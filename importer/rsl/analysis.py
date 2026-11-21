"""Analysing a game: game directory, Ren'Py version, Python generation, checks, the manifest."""
import os
import re
import stat
import struct
import zlib

from .archives import unpack
from .common import examples, is_junk, make_id, read_text, Refused, sha256, VIDEO_EXTS
from .media import MAX_VIDEO_FPS, performance
from .precompile import SDK_VERSION, precompile_scripts
from .recompile import decompile_scripts, recompile_files, stage_py2_sources
from .rpa import extract_rpas, packable, PACKED


LARGE_FILE_BYTES = 2 << 30      # warn: FAT32 SD cards stop at 4 GiB per file
RUNTIME = "renpy-8.5.3"
RUNTIME_VERSION = SDK_VERSION
MANIFEST_FORMAT = "renpy-switch-loader/manifest"
SCRIPT_EXTS = (".rpy", ".rpyc", ".rpa", ".rpym", ".rpymc")
NATIVE_EXTS = (".pyd", ".dll", ".so", ".dylib")
AUDIO_EXTS = (".ogg", ".opus", ".mp3", ".wav", ".flac", ".m4a", ".aac", ".wma", ".mp2")
NO_PLAY_EXTS = (".mid", ".midi")   # the runtime's FFmpeg decodes the rest; Ren'Py has no MIDI
LAUNCHER_EXTS = (".exe", ".sh", ".py", ".app")


def has_scripts(d):
    return any(n.lower().endswith(SCRIPT_EXTS) for n in os.listdir(d))


def find_gamedir(root, depth=0):
    """Ren'Py's path_to_gamedir (renpy.py) on a distribution root, descending through
    one wrapper folder or a macOS .app if needed. Returns (basedir, gamedir)."""
    autorun = os.path.join(root, "Contents", "Resources", "autorun")
    if depth < 3 and os.path.isdir(autorun):   # root is a macOS .app
        return find_gamedir(autorun, depth + 1)
    names = [n for n in sorted(os.listdir(root)) if not is_junk(n)]
    isdir = lambda n: os.path.isdir(os.path.join(root, n)) and not os.path.islink(os.path.join(root, n))
    candidates = []
    for n in names:
        stem, ext = os.path.splitext(n)
        if ext.lower() in LAUNCHER_EXTS:
            candidates += [stem] + [stem[i + 1:] for i, c in enumerate(stem) if c in " _"]
    for c in candidates + ["game", "data"]:
        if c and c != "renpy" and isdir(c):
            return root, os.path.join(root, c)
    if has_scripts(root):
        return root, root
    if depth < 3:
        for n in names:
            if n.endswith(".app") and os.path.isdir(os.path.join(root, n, "Contents", "Resources", "autorun")):
                return find_gamedir(os.path.join(root, n), depth + 1)
        dirs = [n for n in names if isdir(n)]
        if len(dirs) == 1:
            return find_gamedir(os.path.join(root, dirs[0]), depth + 1)
    raise Refused("未找到 Ren'Py 游戏目录（需要含 .rpy/.rpyc/.rpa 文件的 game/）")


def scan(gamedir, top_excludes):
    """Regular files under gamedir as [(posix relpath, size)], plus skipped links."""
    files, skipped = [], []
    for dirpath, dirnames, filenames in os.walk(gamedir):
        top = dirpath == gamedir
        keep = []
        for n in sorted(dirnames):
            p = os.path.join(dirpath, n)
            if is_junk(n) or (top and top_excludes(n)):
                continue
            if os.path.islink(p):
                skipped.append(os.path.relpath(p, gamedir))
            else:
                keep.append(n)
        dirnames[:] = keep
        for n in sorted(filenames):
            p = os.path.join(dirpath, n)
            if is_junk(n) or (top and top_excludes(n)):
                continue
            st = os.lstat(p)
            if stat.S_ISREG(st.st_mode):
                files.append((os.path.relpath(p, gamedir).replace(os.sep, "/"), st.st_size))
            else:
                skipped.append(os.path.relpath(p, gamedir))
    return files, skipped


def renpy_version(basedir, py_hint):
    """Version string from renpy/vc_version.py (8.1+) or renpy/__init__.py, or None."""
    d = os.path.join(basedir, "renpy")
    text = read_text(os.path.join(d, "vc_version.py")) or ""
    m = re.search(r"^version\s*=\s*['\"]([0-9][0-9.]*)['\"]", text, re.M)
    if m:
        return m.group(1)
    compiled = [os.path.join(d, "vc_version.pyc"), os.path.join(d, "vc_version.pyo")]
    cache = os.path.join(d, "__pycache__")
    if os.path.isdir(cache):
        compiled += [os.path.join(cache, n) for n in sorted(os.listdir(cache)) if n.startswith("vc_version.")]
    for p in compiled:
        try:
            with open(p, "rb") as f:
                m = re.search(rb"\b(\d+\.\d+\.\d+\.\d+)\b", f.read(65536))
        except OSError:
            continue
        if m:
            return m.group(1).decode()
    # 7.x and 8.0: version_tuple = (7, 4, 11, vc_version); 7.5/8.0 share one file
    # with both tuples behind `if PY2:`.
    found = [tuple(int(x) for x in t) for t in re.findall(
        r"version_tuple\s*=\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", read_text(os.path.join(d, "__init__.py")) or "")]
    if len(found) > 1 and py_hint in ("2", "3"):
        found = [t for t in found if (t[0] >= 8) == (py_hint == "3")] or found
    return ".".join(map(str, max(found))) if found else None


def lib_python(basedir):
    """Python generations hinted by the engine's lib/ folder: a set of '2'/'3'."""
    lib = os.path.join(basedir, "lib")
    hints = set()
    if os.path.isdir(lib):
        for dirpath, dirnames, filenames in os.walk(lib):
            for n in dirnames + filenames:
                low = n.lower()
                if low.startswith("py3-"):
                    hints.add("3")
                elif low.startswith("py2-") or low in ("python2.7", "pythonlib2.7", "python27.dll"):
                    hints.add("2")
    if os.path.exists(os.path.join(basedir, "python27.dll")):
        hints.add("2")
    return hints


def rpyc_python(path):
    """'3' or '2' from the pickle protocol in slot 1 of a .rpyc, None if unreadable.
    Reads two decompressed bytes; never unpickles."""
    try:
        with open(path, "rb") as f:
            head = f.read(46)
            if head[:10] == b"RENPY RPC2":
                slots = [struct.unpack_from("<III", head, 10 + 12 * i) for i in range(3)] if len(head) == 46 else []
                slot1 = [s for s in slots if s[0] == 1]
                if not slot1:
                    return None
                f.seek(slot1[0][1])
            else:
                f.seek(0)   # pre-RPC2 (Ren'Py 6): the whole file is one zlib stream
            out = zlib.decompressobj().decompress(f.read(4096), 2)
    except (OSError, zlib.error, struct.error):
        return None
    if out[:1] == b"\x80" and len(out) == 2:
        return "3" if out[1] >= 3 else "2"
    return "2" if out[:1] in (b"(", b"}", b"]", b"c") else None   # protocol 0/1


def options_title(gamedir):
    text = read_text(os.path.join(gamedir, "options.rpy")) or ""
    m = re.search(r"config\.name\s*=\s*(?:_\(\s*)?[uU]?(['\"])((?:\\.|(?!\1).)*)\1", text)
    return re.sub(r"\\(.)", r"\1", m.group(2)).strip() if m else None


def analyze(root, kind, source, game_id=None, extract_warnings=(), recompile=False, optimize=False,
            workdir=None, decompile=None, rpycdec_python=None, unrpa=False, *, precompile=False):
    """Returns (manifest, gamedir, files, {exported rel: path outside gamedir}).
    decompile: None, or the DECOMPILERS tool to rebuild sourceless scripts with; it
    implies recompile. unrpa: the game's .rpa archives are extracted into a staged copy
    in workdir (the source is untouched) and the import works from that, so scripts a
    Python 2 game shipped inside an archive (Steam's DDLC) become decompilable."""
    recompile = recompile or bool(decompile)
    warnings = list(extract_warnings)
    reasons = []
    basedir, gamedir, files, skipped = game_files(root)
    if skipped:
        warnings.append("跳过了 %d 个链接或特殊文件：%s" % (len(skipped), examples(skipped)))
    if unrpa:
        if not workdir:
            raise Refused("--unrpa 需要一个暂存目录（命令行与窗口都会给出）")
        # basedir stays the original: the engine (renpy/, lib/) is detected there, and
        # title_and_id names the game from it; only file access moves to the staging.
        gamedir, files = extract_rpas(gamedir, files, workdir, warnings)
    exts = {}
    for rel, size in files:
        ext = os.path.splitext(rel)[1].lower()
        exts[ext] = exts.get(ext, 0) + 1
    if not any(exts.get(e) for e in SCRIPT_EXTS):
        raise Refused("%s 中没有 Ren'Py 脚本（.rpy/.rpyc/.rpa）" % gamedir)

    # Python generation: engine lib/ first, then the .rpyc pickles.
    hints = {}
    lib = lib_python(basedir)
    if lib:
        hints["lib/"] = "/".join(sorted(lib))
    rpyc = {rpyc_python(os.path.join(gamedir, rel)) for rel, _ in files if rel.lower().endswith(".rpyc")} - {None}
    if rpyc:
        hints[".rpyc"] = "/".join(sorted(rpyc))
    py_hint = next((h for h in (hints.get("lib/"), hints.get(".rpyc")) if h in ("2", "3")), None)

    version = renpy_version(basedir, py_hint)
    parent = os.path.dirname(os.path.abspath(basedir))
    if version is None and kind == "directory" and os.path.isdir(os.path.join(parent, "renpy")):
        version = renpy_version(parent, py_hint)
        if version:
            warnings.append("Ren'Py 版本读自外层 %r 文件夹的 renpy/（SDK 项目或直接给出的 "
                            "game/ 文件夹）" % os.path.basename(parent))
    vt = tuple(int(x) for x in version.split(".")[:3]) if version else None

    python = ("3" if vt[0] >= 8 else "2") if vt else (py_hint or "unknown")
    seen = set("/".join(hints.values()).split("/")) | {python}
    if len(seen - {"", "unknown"}) > 1:
        warnings.append("Python 版本线索相互冲突：%s；采用 %s" % (
            ", ".join("%s 指向 %s" % kv for kv in sorted(hints.items())), python))

    if vt and vt[0] == 8:
        if vt[:2] < RUNTIME_VERSION[:2]:
            warnings.append("由 Ren'Py %s 构建；只有 8.5.x 在硬件上运行过，8.5.3 能否加载更旧的 "
                            "8.x .rpyc 尚未测试（docs/COMPATIBILITY.md）" % version)
        elif vt > RUNTIME_VERSION:
            warnings.append("由 Ren'Py %s 构建，比运行时（8.5.3）更新" % version)
    elif vt and vt[0] > 8:
        reasons.append("Ren'Py %s：还没有支持 Ren'Py %d 的运行时" % (version, vt[0]))
    elif python == "3":
        warnings.append("Ren'Py 版本未知（没有 renpy/ 文件夹）；Python 3 的 .rpyc 文件 "
                        "意味着是 Ren'Py 8.x")
    elif python != "2":
        reasons.append("无法判断 Ren'Py 版本或 Python 世代（没有 renpy/、lib/ "
                       "或 .rpyc 文件）")
    if recompile and python != "2":
        raise Refused("--recompile 仅适用于 Python 2 游戏（Ren'Py 7 或更旧）")
    decompiled = {}
    suggest = {}   # importer options that would fix something found here, with why
    if recompile:
        files, orphans = recompile_files(files, warnings, decompile, gamedir)
        if orphans:
            if decompile == "unrpyc" and vt and vt < (6, 18):
                raise Refused("--decompile：unrpyc 只能反编译 Ren'Py 6.18 及更新版本，而非 %s" % version)
            decompiled = decompile_scripts(gamedir, orphans, decompile, workdir, warnings, rpycdec_python)
            files += [(r, os.path.getsize(p)) for r, p in sorted(decompiled.items())]
    elif python == "2":
        try:
            orphans = recompile_files(files, [], decompile=True, gamedir=gamedir)[1]
            reasons.append("%s 使用 Python 2；还没有 Python 2 运行时（附带 .rpy 源码可尝试 --recompile，"
                           "否则尝试 --decompile）" % ("Ren'Py " + version if vt else "这个游戏"))
        except Refused:
            orphans = None   # an .rpa holds compiled scripts or cannot be checked
            reasons.append("%s 使用 Python 2，且其脚本在 .rpa 压缩包内；还没有 Python 2 运行时"
                           "——可尝试 --unrpa 解开压缩包，然后 --decompile 反编译"
                           % ("Ren'Py " + version if vt else "这个游戏"))
            suggest["unrpa"] = ("脚本在 .rpa 压缩包内：解开它们成散文件（到导出暂存区，源目录不变），"
                                "然后即可反编译并用 Python 3 编译游戏（实验性功能）")
        if orphans is not None and all(r.startswith("tl/") for r in orphans):
            suggest["recompile"] = ("附带全部 .rpy 源码的 Python 2 游戏：Ren'Py 8.5.3 可以用 "
                                    "Python 3 编译它们，因此可以导出（实验性功能）")
        if orphans:
            suggest["decompile"] = ("%d 个编译后的脚本从未被反编译：它们没有 .rpy 源码 "
                                    "（%s）。unrpyc 或 rpycdec 可以重建它们，然后 Ren'Py 8.5.3 会用 "
                                    "Python 3 编译游戏（实验性功能）" % (len(orphans), examples(orphans)))
    exts = {}
    for rel, _ in files:
        ext = os.path.splitext(rel)[1].lower()
        exts[ext] = exts.get(ext, 0) + 1

    native = [r for r, _ in files if r.lower().endswith(NATIVE_EXTS)]
    if native:
        warnings.append("无法在 Switch 上加载的原生代码：%s；游戏一旦导入就会失败" %
                        examples(native))
    if os.path.isdir(os.path.join(gamedir, "python-packages")):
        warnings.append("存在 game/python-packages：必须是纯 Python 3.12，并避免使用 ctypes、"
                        "ssl、sqlite3、mmap、subprocess、multiprocessing")
    pys = [r for r, _ in files if r.endswith(".py") and not r.endswith("_ren.py")
           and not r.startswith("python-packages/")]
    if pys:
        warnings.append("game/ 中有 %d 个 Python 模块（%s）：与 python-packages 同样的限制" % (
            len(pys), examples(pys)))
    rpa_bad = []
    for rel, _ in files:
        if rel.lower().endswith(".rpa"):
            with open(os.path.join(gamedir, rel), "rb") as f:
                if f.read(8) not in (b"RPA-3.0 ", b"RPA-2.0 "):
                    rpa_bad.append(rel)
    if rpa_bad:
        warnings.append("压缩包头非标准：%s；游戏需要自己的加载器（未测试）" % examples(rpa_bad))

    media = {}
    for rel, size in files:
        ext = os.path.splitext(rel)[1].lower()
        if ext in AUDIO_EXTS or ext in VIDEO_EXTS or ext in NO_PLAY_EXTS:
            media[ext] = media.get(ext, 0) + 1
    midi = [r for r, _ in files if r.lower().endswith(NO_PLAY_EXTS)]
    if midi:
        warnings.append("Ren'Py 无法播放 MIDI：%s" % examples(midi))
    issues, convert, extra, coverage, would_fix = performance(gamedir, files, warnings, optimize, workdir)
    if would_fix:
        suggest["optimize"] = ("%d 个视频高于 %d fps（%s）：以相同尺寸重编码为 %d fps，"
                               "可减少 Switch 的 CPU 负担和发热" % (len(would_fix), MAX_VIDEO_FPS,
                                                                 examples(would_fix, 2), MAX_VIDEO_FPS))
    extra.update(decompiled)
    source_files = {"count": len(files) - len(decompiled),
                    "total_bytes": sum(size for rel, size in files if rel not in decompiled)}
    py2_converted = False
    if recompile and python == "2":
        extra, converted_count = stage_py2_sources(gamedir, files, extra, workdir)
        py2_converted = True
        files = [(rel, os.path.getsize(extra[rel]) if rel in extra else size) for rel, size in files]
        if converted_count:
            warnings.append("已将 %d 个 Python 2 脚本的代码块迁移为 Python 3，并修复旧版语法空块；"
                            "源文件未修改" % converted_count)
    precompiled = {}
    input_hashes = script_hashes(gamedir, files)
    if precompile:
        if reasons:
            raise Refused("--precompile：请先解决游戏不受支持的原因：" + reasons[0])
        if workdir is None:
            raise Refused("--precompile 需要暂存目录")
        precompiled = precompile_scripts(gamedir, files, extra, workdir)
        extra.update(precompiled)
        files = [(rel, size) for rel, size in files if rel not in precompiled]
        files.extend((rel, os.path.getsize(path)) for rel, path in sorted(precompiled.items()))
        exts[".rpyc"] = sum(rel.lower().endswith(".rpyc") for rel, _ in files)
    big = [r for r, s in files if s >= LARGE_FILE_BYTES]
    if big:
        warnings.append("存在 2 GiB 或更大的文件（%s）：FAT32 SD 卡无法容纳 4 GiB 或更大的文件；"
                        "请使用 exFAT" % examples(big))
    packed = [r for r, _ in files if packable(r)]
    if packed:
        warnings.append("%d 个文件名含非 ASCII 字符（%s），将打包进 %s.rpa：Switch 的 SD 卡无法列出 "
                        "这种名称（硬件运行 20）" % (len(packed), examples(packed), PACKED))
    loose = [r for r, _ in files if not r.isascii() and not packable(r)]
    if loose:
        warnings.append("脚本或压缩包名含非 ASCII 字符（%s）：Ren'Py 无法从压缩包加载它们，"
                        "Switch 的 SD 卡也无法列出它们；游戏可能找不到这些文件" % examples(loose))

    title, default_id = title_and_id(root, kind, source, basedir, gamedir)
    game_id = default_id if game_id is None else game_id
    status = "unsupported" if reasons else "experimental"
    manifest = {
        "format": MANIFEST_FORMAT,
        "version": 1,
        "id": game_id,
        "title": title,
        "source": {"type": kind, "name": os.path.basename(os.path.normpath(source))},
        "renpy_version": version,
        "python": python,
        "runtime": RUNTIME if status == "experimental" else None,
        "status": status,
        "reasons": reasons,
        "warnings": warnings,
        "files": {"count": len(files), "total_bytes": sum(s for _, s in files),
                  "rpa": exts.get(".rpa", 0), "rpy": exts.get(".rpy", 0),
                  "rpyc": exts.get(".rpyc", 0), "rpym": exts.get(".rpym", 0)},
        "media": dict(sorted(media.items())),
        "recompile": recompile,
        "unrpa": bool(unrpa),
        "python2_converted": py2_converted,
        "precompiled": sorted(precompiled),
        "suggested_options": suggest,
        "decompiled": sorted(decompiled),
        "performance": {"issues": issues, "converted": convert, "coverage": coverage},
        # This identifies the original inputs for library comparison, not generated outputs.
        "sha256": input_hashes,
        "source_files": source_files,
    }
    return manifest, gamedir, files, extra


def game_files(root):
    """The game directory and the files an export takes from it: (basedir, gamedir,
    [(rel, size)], skipped links)."""
    basedir, gamedir = find_gamedir(root)
    flat = gamedir == basedir   # scripts sit in basedir: leave engine/launchers out
    excludes = (lambda n: n == "saves" or n in ("renpy", "lib") or n.lower().endswith(
        (".exe", ".app", ".sh"))) if flat else (lambda n: n == "saves")
    return (basedir, gamedir) + scan(gamedir, excludes)


def title_and_id(root, kind, source, basedir, gamedir):
    """config.name or the folder/archive name, and the id made from it."""
    if basedir == root and kind != "directory":
        folder = os.path.splitext(os.path.basename(source))[0]
    else:
        folder = os.path.basename(os.path.normpath(basedir))
        if gamedir == basedir and folder == "game":   # a game/ folder given directly
            folder = os.path.basename(os.path.dirname(os.path.abspath(basedir)))
        app = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(basedir))))
        if folder == "autorun" and app.endswith(".app"):   # <Name>.app/Contents/Resources/autorun
            folder = os.path.basename(app)[:-4]
    title = options_title(gamedir) or folder
    return title, make_id(title) if make_id(title) != "game" else make_id(folder)


def script_hashes(gamedir, files):
    return {rel: sha256(os.path.join(gamedir, rel)) for rel, _ in files if rel.lower().endswith((".rpa", ".rpyc"))}


def load(source, tmp, game_id=None, recompile=False, optimize=False, decompile=None, rpycdec_python=None,
         unrpa=False, *, precompile=False):
    """unpack() then analyze(): (manifest, gamedir, files, extra). An archive is unpacked
    into tmp, which must stay until export() is done."""
    warnings = []
    root, kind = unpack(source, tmp, warnings)
    return analyze(root, kind, source, game_id, warnings, recompile, optimize, tmp, decompile,
                   rpycdec_python, unrpa, precompile=precompile)


def report(m):
    lines = ["%s  (id: %s)" % (m["title"], m["id"]),
             "  来源：  %s %s" % (m["source"]["type"], m["source"]["name"]),
             "  Ren'Py:  %s   Python：%s" % (m["renpy_version"] or "unknown", m["python"]),
             "  状态：  %s" % (m["status"].upper() + ("（运行时 " + m["runtime"] + "）" if m["runtime"] else "")),
             "  文件：   game/ 中 %d 个，%.1f MB（rpa %d，rpy %d，rpyc %d，rpym %d）" % (
                 m["files"]["count"], m["files"]["total_bytes"] / 1e6, m["files"]["rpa"],
                 m["files"]["rpy"], m["files"]["rpyc"], m["files"]["rpym"]),
             "  媒体：   %s" % (", ".join("%s %d" % kv for kv in m["media"].items()) or "无")]
    pictures = ["%s%s" % (m[k], "（已选择）" if m.get(k + "_custom") else "") for k in ("cover",) if m.get(k)]
    lines.append("  图片： %s" % (", ".join(pictures) or "无；加载器将显示占位图"))
    for key in ("reasons", "warnings"):
        if m[key]:
            lines.append("  %s:" % ("无法支持的原因" if key == "reasons" else "警告"))
            lines += ["    - " + w for w in m[key]]
    cov = m["performance"]["coverage"]
    lines.append("  已检查：%d 个散装 + %d 个压缩包内图像/视频；%d 个无法读取的压缩包" % (
        cov["loose_media"], cov["archived_media"], len(cov["locked_archives"])))
    issues = m["performance"]["issues"]
    lines.append("  Switch 性能：%s" % ("%d 条问题，详见加载器" % len(issues) if issues else "正常"))
    lines += ["    ! " + i for i in issues]
    lines += ["  建议：--%s：%s" % kv for kv in m.get("suggested_options", {}).items()]
    return "\n".join(lines)
