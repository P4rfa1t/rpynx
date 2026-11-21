"""Explicit desktop compilation with the same Ren'Py release as the Switch runtime."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen
import zipfile

from .archives import extract_zip
from .common import Refused
from .tools import run


SDK_VERSION = (8, 5, 3)
SDK_NAME = "renpy-8.5.3-sdk"
SDK_URL = "https://www.renpy.org/dl/8.5.3/" + SDK_NAME + ".zip"
# https://www.renpy.org/dl/8.5.3/checksums.txt
SDK_SHA256 = "ff57648f9c04f27e381c48af6d8e3ee3cdec296bed4d3831f47f09b0a71b505e"
SDK_SIZE = 163008121
DOWNLOAD_TIMEOUT = 1800
COMPILE_TIMEOUT = 900
RPYC_MAGIC = b"_2025-07-06"  # Ren'Py 8.5.3 renpy/script.py
SOURCE_SUFFIXES = (".rpy", ".rpym", "_ren.py")


def cache_path():
    return Path(os.environ.get("LOCALAPPDATA") or (Path.home() / "AppData" / "Local")) / "rpynx" / "sdk" / (SDK_NAME + ".zip")


def _verified(path):
    if not path.is_file() or path.stat().st_size != SDK_SIZE:
        return False
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest() == SDK_SHA256


def _sdk_archive():
    cached = cache_path()
    if _verified(cached):
        return cached
    cached.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="download-", dir=cached.parent) as tmp:
        candidate = Path(tmp) / cached.name
        try:
            deadline = time.monotonic() + DOWNLOAD_TIMEOUT
            with urlopen(SDK_URL, timeout=30) as reply, candidate.open("wb") as f:
                remaining = SDK_SIZE
                while remaining:
                    if time.monotonic() > deadline:
                        raise TimeoutError("下载超时")
                    block = reply.read1(min(1 << 20, remaining))
                    if not block:
                        break
                    f.write(block)
                    remaining -= len(block)
                if not remaining and reply.read(1):
                    raise ValueError("SDK 下载大小不符")
        except (OSError, ValueError) as e:
            raise Refused("--precompile：下载 SDK 失败：%s" % e) from e
        if not _verified(candidate):
            raise Refused("--precompile：SDK 的大小或 SHA-256 不匹配")
        os.replace(candidate, cached)
    return cached


def _compiled_rel(rel):
    if rel.lower().endswith("_ren.py"):
        return rel[:-7] + ".rpyc"
    return rel + "c"


def _compile_error(project, result):
    for folder in (project, project / "game"):
        for name in ("errors.txt", "traceback.txt"):
            path = folder / name
            if path.is_file():
                with path.open(encoding="utf-8-sig", errors="replace") as f:
                    return f.read(5000).strip()
    return (result.stderr.strip() or result.stdout.strip() or "exit %d" % result.returncode)[-5000:]


def precompile_scripts(gamedir, files, extra, workdir):
    """Run game init only with explicit consent. A temp copy is not a sandbox.

    Return overrides for matching .rpyc/.rpymc only. Do not export SDK caches or
    anything game init wrote to the staged project (including saves or changed sources).
    """
    if sys.platform != "win32":
        raise Refused("--precompile：桌面预编译目前仅支持 Windows")
    sources = [(rel, _compiled_rel(rel)) for rel, _ in files
               if rel.lower().endswith(SOURCE_SUFFIXES) and not rel.startswith("cache/")]
    if not sources:
        raise Refused("--precompile：没有可预编译的松散脚本；如脚本在归档中，请先解包或反编译")
    if len({dest.casefold() for _, dest in sources}) != len(sources):
        raise Refused("--precompile：多个源脚本对应同一编译文件")

    root = Path(tempfile.mkdtemp(prefix="precompile-", dir=workdir))
    project = root / "project"
    target = project / "game"
    digests = {}
    for rel, _ in files:
        if rel.startswith("cache/"):
            continue
        source = Path(extra.get(rel) or os.path.join(gamedir, *rel.split("/")))
        staged = target / rel
        staged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, staged)
        if rel.lower().endswith(SOURCE_SUFFIXES):
            digests[rel] = hashlib.md5(staged.read_bytes() + RPYC_MAGIC).digest()

    # Extract a fresh copy from a verified ZIP, not a mutable executable cache.
    sdk_root = root / "sdk"
    sdk_root.mkdir()
    try:
        extract_zip(_sdk_archive(), str(sdk_root), [])
    except (OSError, zipfile.BadZipFile) as e:
        raise Refused("--precompile：无法解包 Ren'Py SDK：%s" % e) from e
    sdk = sdk_root / SDK_NAME
    launcher = sdk / "renpy.py"
    interpreter = sdk / "lib" / "py3-windows-x86_64" / "python.exe"
    if not (interpreter.is_file() and launcher.is_file()):
        raise Refused("--precompile：SDK 缺少 Windows x64 编译器")

    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("PYTHON", "RENPY"))}
    env.update(SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
    cmd = [str(interpreter), "-E", "-s", "-B", str(launcher), str(project), "compile",
           "--keep-orphan-rpyc", "--savedir", str(root / "saves")]
    try:
        result = run(cmd, cwd=str(project), env=env, timeout=COMPILE_TIMEOUT)
    except subprocess.TimeoutExpired as e:
        raise Refused("--precompile：Ren'Py 编译超过 %d 秒" % COMPILE_TIMEOUT) from e
    except OSError as e:
        raise Refused("--precompile：无法启动 Ren'Py SDK：%s" % e) from e
    if result.returncode or any((folder / name).exists() for folder in (project, target)
                                for name in ("errors.txt", "traceback.txt")):
        raise Refused("--precompile：Ren'Py 编译失败：\n" + _compile_error(project, result))

    outputs = {}
    for source, rel in sources:
        compiled = target / rel
        try:
            with compiled.open("rb") as f:
                header = f.read(10)
                f.seek(-16, os.SEEK_END)
                digest = f.read(16)
        except OSError as e:
            raise Refused("--precompile：没有生成有效的 %s" % rel) from e
        if header != b"RENPY RPC2" or digest != digests[source]:
            raise Refused("--precompile：%s 与将导出的源脚本不匹配" % rel)
        outputs[rel] = str(compiled)
    return outputs
