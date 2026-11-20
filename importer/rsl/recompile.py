"""--recompile and --decompile: Python 2 games for the Ren'Py 8.5.3 runtime."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

from .common import examples, read_text, Refused
from .rpa import rpa_index
from .tools import find_tool, run


# --decompile: third_party/unrpyc/fetch.sh puts unrpyc here; it runs on the importer's Python.
UNRPYC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                      "build", "unrpyc", "unrpyc", "unrpyc.py")
DECOMPILERS = ("unrpyc", "rpycdec")   # --decompiler; the first is the default (docs/IMPORTER.md compares them)
DECOMPILE_TIMEOUT = 600           # seconds for all of a game's sourceless scripts


def rpa_with_scripts(gamedir, files):
    """The first .rpa that holds compiled scripts (.rpyc/.rpymc), or that cannot be
    checked, as a reason string. Ren'Py 8 loads .rpyc from inside an archive, and a
    Python 2 script there cannot be loaded or rewritten, so recompiling such a game
    would crash it; a media-only archive is fine. The index is read as plain data
    (rpa.rpa_index), never unpickled."""
    for rel, _ in sorted(f for f in files if f[0].lower().endswith(".rpa")):
        try:
            index = rpa_index(os.path.join(gamedir, rel))
        except (ValueError, zlib.error, OSError, KeyError, IndexError, TypeError) as e:
            return "%s（其索引无法读取：%s）" % (rel, e)
        if index is None:
            return "%s（自定义格式；其索引无法读取）" % rel
        scripts = sorted(name for name in index if name.lower().endswith((".rpyc", ".rpymc")))
        if scripts:
            return "%s 载有 %d 个已编译脚本（%s）" % (rel, len(scripts), examples(scripts, 3))
    return None


def recompile_files(files, warnings, decompile=False, gamedir=None):
    """--recompile: drop the Python 2 .rpyc so Ren'Py 8.5.3 compiles the .rpy sources
    with Python 3. Refuses when scripts could be missing their source, unless they are to
    be decompiled, and when an .rpa holds compiled scripts or cannot be checked (see
    rpa_with_scripts). Returns (files to export, compiled scripts to decompile)."""
    names = {r for r, _ in files}
    if gamedir is not None:
        why = rpa_with_scripts(gamedir, files)
        if why:
            raise Refused("--recompile: %s；反编译器无法处理压缩包内部 - "
                          "请自行解压后导入该文件夹" % why)
    compiled = [r for r in names if r.lower().endswith((".rpyc", ".rpymc"))]
    orphans = sorted(r for r in compiled if r[:-1] not in names)
    outside = [r for r in orphans if not r.startswith("tl/")]
    if outside and not decompile:
        raise Refused("--recompile：%d 个已编译脚本没有 .rpy 源文件（%s）；请加 --decompile "
                      "用 unrpyc 重建它们" % (len(outside), examples(outside)))
    if orphans and not decompile:
        warnings.append("--recompile 略过了 %d 个没有源文件的已编译翻译文件（%s）；"
                        "这些翻译可能会缺失" % (len(orphans), examples(orphans)))
    warnings.append("已重新编译：Ren'Py 8.5.3 会在首次启动时用 Python 3 编译 .rpy 源码（仅首次较慢）。"
                    "Python 2 代码在 Python 3 下可能不报错而行为不同（例如整数相除 / 会得到浮点数），"
                    "请检查游戏逻辑；原游戏的存档将无法读取")
    return recompiled(files), orphans if decompile else []


def recompiled(files):
    """What --recompile exports: no compiled scripts, no cache/."""
    return [(r, s) for r, s in files if not r.lower().endswith((".rpyc", ".rpymc")) and not r.startswith("cache/")]


def find_unrpyc():
    """unrpyc.py, or None. It needs a Python to run on, which a packaged importer is not."""
    return UNRPYC if os.path.isfile(UNRPYC) and not getattr(sys, "frozen", False) else None


def decompile_scripts(gamedir, compiled, tool, workdir, warnings, rpycdec_python=None):
    """--decompile: rebuild the .rpy of compiled scripts with tool, "unrpyc" or "rpycdec"
    (DECOMPILERS). Both unpickle them, so they run in their own process on copies in
    workdir, never in the importer. A script the tool cannot decompile is refused, or under
    tl/ left out with a warning, as recompile_files() does with sourceless ones.
    rpycdec_python: an interpreter that has rpycdec, else rpycdec on PATH.
    Returns {decompiled .rpy: path}."""
    work = tempfile.mkdtemp(prefix="decompile-", dir=workdir)
    copies = os.path.join(work, "game")
    env = None
    if tool == "unrpyc":
        if not find_unrpyc():
            raise Refused("--decompile：未找到 unrpyc；请先运行 third_party/unrpyc/fetch.sh 获取，"
                          "并用 Python 3.9+ 运行导入器（打包版无法运行它）")
        # --no-init-offset: its init offset guess is only safe for Ren'Py 8 (the output is the same game).
        cmd = [sys.executable, UNRPYC, "--no-init-offset", copies]
    else:
        cmd = [rpycdec_python, "-m", "rpycdec"] if rpycdec_python else [find_tool(("rpycdec",))]
        if not cmd[0]:
            raise Refused("--decompile：PATH 上未找到 rpycdec；请用 `pip install rpycdec` 安装"
                          "（需要 Python 3.10+），或用 --rpycdec-python 指定其解释器")
        cmd, env = cmd + ["decompile", copies], dict(os.environ, RPYCDEC_NO_WARNING="1")
    for rel in compiled:
        target = os.path.join(copies, *rel.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(os.path.join(gamedir, *rel.split("/")), target)
    try:
        # cwd holds only the copies: `python -m` puts it on sys.path.
        r = run(cmd, cwd=work, timeout=DECOMPILE_TIMEOUT, env=env)
    except subprocess.TimeoutExpired:
        raise Refused("--decompile：%s 未能在 %d 秒内完成" % (tool, DECOMPILE_TIMEOUT))
    except OSError as e:
        raise Refused("--decompile：无法运行 %s：%s" % (cmd[0], e))
    if r.returncode:   # rpycdec stops at the first script it cannot read; unrpyc exits 0 then
        err = [l for l in r.stderr.splitlines() if l.strip()] or ["exit %d" % r.returncode]
        why = next((l for l in err if l.startswith("ERROR:")), err[-1]).replace(copies + os.sep, "")
        raise Refused("--decompile：%s 失败：%s" % (tool, why[:300]))
    # unrpyc goes on, logging each failure as "Error while decompiling <resolved path>:" and a traceback.
    errors = {os.path.normcase(p) for p in re.findall(r"^Error while decompiling (.+):$", r.stdout, re.M)}
    out, failed = {}, []
    for rel in compiled:
        src = os.path.join(copies, *rel.split("/"))
        if os.path.normcase(os.path.realpath(src)) in errors or not os.path.isfile(src[:-1]):
            failed.append(rel)
        else:
            out[rel[:-1]] = src[:-1]
    cause = re.search(r"^Traceback \(most recent call last\):\n(?:[ \t].*\n)*(.+)$", r.stdout, re.M)
    cause = " (first error: %s)" % cause.group(1)[:200] if cause else ""
    outside = [rel for rel in failed if not rel.startswith("tl/")]
    if outside:
        raise Refused("--decompile：%s 无法反编译 %d 个脚本中的 %d 个（%s）%s；游戏需要它们"
                      % (tool, len(outside), len(compiled), examples(outside), cause))
    if failed:
        warnings.append("%s 无法反编译 %d 个翻译文件（%s）%s；它们被略过，这些翻译可能会缺失"
                        % (tool, len(failed), examples(failed), cause))
    mark = {"unrpyc": "<<<COULD NOT DECOMPILE", "rpycdec": "# <unrecognized:"}[tool]
    partial = [rel for rel, p in out.items() if mark in read_text(p)]
    if out:
        warnings.append("用 %s 反编译了 %d 个没有 .rpy 源文件的脚本（%s）。反编译结果可能与原文件不同："
                        "注释和格式会丢失，语句也可能被误读，请通玩整个游戏以确认"
                        % (tool, len(out), examples(out)))
    if partial:
        warnings.append("%s 无法反编译 %s 中的部分语句；这些语句以注释形式保留"
                        "（搜索 %r），游戏可能在该处出错" % (tool, examples(partial), mark))
    return out
