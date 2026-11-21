"""--recompile and --decompile: Python 2 games for the Ren'Py 8.5.3 runtime."""
import ast
import functools
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


def bundled_tool(name):
    """The path of a decompiler embedded in the packaged program (build_app.py adds
    unrpyc/rpycdec as binaries), or None. PyInstaller's --onefile extracts them beside
    the program at start (sys._MEIPASS); a --onedir app has them in its MacOS folder."""
    if not getattr(sys, "frozen", False):
        return None
    base = getattr(sys, "_MEIPASS", None) or os.path.dirname(sys.executable)
    for exe in (name, name + ".exe"):
        path = os.path.join(base, exe)
        if os.path.isfile(path):
            return path
    return None


def rpa_with_scripts(gamedir, files):
    """The first .rpa that holds compiled scripts (.rpyc/.rpymc), or that cannot be
    checked, as a reason string. Ren'Py 8 loads .rpyc from inside an archive, and a
    Python 2 script there cannot be loaded or rewritten, so recompiling such a game
    would crash it; a media-only archive is fine. The index is read as plain data
    (rpa.rpa_index), never unpickled."""
    for rel, _ in sorted(f for f in files if f[0].lower().endswith(".rpa") and not f[0].startswith("../")):
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
                          "请加 --unrpa 解开压缩包（解开到导出暂存区，源目录不变）" % why)
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
    """Drop compiled scripts and caches from game/, preserving assets beside it."""
    return [(r, s) for r, s in files if r.startswith("../") or (
        not r.lower().endswith((".rpyc", ".rpymc")) and not r.startswith("cache/"))]


_PYTHON_BLOCK = re.compile(
    r"^(?P<indent> *)(?:(?:init(?: +[-+]?\d+)? +|translate +\w+ +)?python"
    r"(?: +(?:early|hide|in +[\w.]+))*|\$)"
    r"(?:(?: *: *(?:#.*)?)|(?<=\$) +(?P<inline>.+))$"
)


@functools.lru_cache(maxsize=1)
def _py2_refactor():
    try:
        from fissix.refactor import RefactoringTool
    except ImportError as e:
        raise Refused("Python 2 源码转换需要 Fissix；请安装 importer/requirements.txt 中的依赖") from e
    # Syntax and the standard-library moves a game needs to run on Python 3.12: print,
    # except, xrange, dict iteration, imports (urllib2, cPickle, StringIO, ...), itertools.
    names = ("print", "exec", "except", "raise", "numliterals", "ne", "xrange",
             "dict", "unicode", "long", "basestring", "raw_input", "has_key",
             "standarderror", "tuple_params", "xreadlines", "repr",
             "imports", "urllib", "itertools", "itertools_imports", "next", "operator")
    return RefactoringTool(["fissix.fixes.fix_" + name for name in names])


# Python 2 modules Python 3 removed, with the exact replacement for each name used.
# Fissix leaves `sets` alone; anything not listed here is refused, never guessed.
_REMOVED_MODULES = {"sets": {"Set": "set", "ImmutableSet": "frozenset"}}


def _removed_module_calls(source, filename):
    """Rewrite or refuse uses of Python 2 modules that no longer exist (Katawa Shoujo's
    `import sets` / `sets.Set()`), so the game fails here with the file name rather than
    on the console with a ModuleNotFoundError."""
    out, problems = [], []
    for number, line in enumerate(source.splitlines(keepends=True), 1):
        stripped = line.strip()
        ending = line[len(line.rstrip("\r\n")):]
        module = next((m for m in _REMOVED_MODULES
                       if re.fullmatch(r"import\s+" + m + r"\s*;?", stripped)), None)
        if module is not None:
            out.append("pass" + ending if line[:1].isspace() else "")
            continue
        # `import a, sets` keeps a and drops only the gone name.
        m = re.fullmatch(r"(?P<head>\s*import\s+)(?P<names>[^#]+?)(?P<tail>\s*(?:#.*)?)", line, re.S)
        if m:
            kept = [n.strip() for n in m["names"].split(",")
                    if n.strip() not in _REMOVED_MODULES]
            if len(kept) != len(m["names"].split(",")):
                line = (m["head"] + ", ".join(kept) + m["tail"] + ending) if kept else "pass" + ending
        for module, names in _REMOVED_MODULES.items():
            for old, new in names.items():
                line = line.replace(module + "." + old, new)
            if re.search(r"(?<![\w.])" + re.escape(module) + r"\s*\.", line):
                problems.append("%s:%d 使用了已从 Python 3 移除的模块 %r" % (filename, number, module))
        out.append(line)
    if problems:
        raise Refused("无法自动转换：" + problems[0])
    return "".join(out)


# Ren'Py API names that changed between the 6.x/7.x generation and 8.5.3, applied as
# exact text edits inside Python blocks only. Katawa Shoujo calls
# renpy.context().main_menu - 8.5.3 stores the flag as _main_menu (00gamemenu.rpy's
# _enter_menu) - and its own version check (int(version.split(".")[1]) > 10) cannot
# notice an 8.5.3 install, so it reads the old name there.
_RENPY_API = (
    ("renpy.context().main_menu", "renpy.context()._main_menu"),
)


def _renpy_api_calls(source):
    for old, new in _RENPY_API:
        source = source.replace(old, new)
    return source


def _text_file_calls(source, tree):
    """Select legacy text loops with an explicit string separator (DDLC poemwords.txt).
    Do not change other reads, including byte-oriented reads of a .txt file."""
    edits = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.With):
            continue
        for item in node.items:
            call = item.context_expr
            binding = item.optional_vars
            if not (isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
                    and isinstance(call.func.value, ast.Name) and call.func.value.id == "renpy"
                    and call.func.attr == "file" and len(call.args) == 1 and not call.keywords
                    and isinstance(call.args[0], ast.Constant) and isinstance(call.args[0].value, str)
                    and call.args[0].value.lower().endswith((".txt", ".csv", ".tsv"))
                    and isinstance(binding, ast.Name)):
                continue
            for loop in node.body:
                if not (isinstance(loop, ast.For) and isinstance(loop.iter, ast.Name)
                        and loop.iter.id == binding.id and isinstance(loop.target, ast.Name)):
                    continue
                if any(isinstance(x, ast.Call) and isinstance(x.func, ast.Attribute)
                       and isinstance(x.func.value, ast.Name) and x.func.value.id == loop.target.id
                       and x.func.attr == "split" and x.args and isinstance(x.args[0], ast.Constant)
                       and isinstance(x.args[0].value, str) for x in ast.walk(loop)):
                    edits.append((call.end_lineno - 1, call.end_col_offset - 1))
                    break
    if not edits:
        return source
    lines = source.splitlines(keepends=True)
    for row, col in sorted(set(edits), reverse=True):
        raw = lines[row].encode("utf-8")  # AST columns are UTF-8 offsets.
        lines[row] = (raw[:col] + b', encoding="utf-8"' + raw[col:]).decode("utf-8")
    return "".join(lines)


def convert_py2_block(source, filename):
    """Refactor one Python block without evaluating it or touching Ren'Py DSL."""
    try:
        tool = _py2_refactor()
        try:
            updated = tool.refactor_string(source if source.endswith("\n") else source + "\n", filename)
        except Exception:
            # Fissix uses an older grammar; preserve already-valid newer syntax.
            ast.parse(source, filename)
            return source
        if updated is None:
            return source
        converted = str(updated)
        converted = _text_file_calls(converted, ast.parse(converted, filename))
        converted = _renpy_api_calls(converted)
        converted = _removed_module_calls(converted, filename)
        ast.parse(converted, filename)   # the migration must still be valid Python 3
        return converted
    except Refused:
        raise
    except Exception as e:
        raise Refused("无法将 Python 2 代码转换为 Python 3（%s：%s）" % (filename, e)) from e


# A Ren'Py logical line may span unindented physical lines inside a string.
_SCRIPT_TOKENS = re.compile(r'''\#[^\n]*|("""|\x27\x27\x27)(?:\\[\s\S]|(?!\1)[^\\])*\1|"(?:\\[\s\S]|[^"\\\n])*"|'(?:\\[\s\S]|[^'\\\n])*'|[()[\]{}]|\\\n|\n''', re.X)


def _logical_rows(source):
    """Rows starting inside literals and rows continuing a logical statement."""
    import bisect

    offsets = [m.start() for m in re.finditer("\n", source)]
    literals, continuation = set(), set()
    depth = 0
    for m in _SCRIPT_TOKENS.finditer(source):
        token = m[0]
        if token in ("(", "[", "{"):
            depth += 1
        elif token in (")", "]", "}"):
            depth = max(0, depth - 1)
        elif token == "\\\n" or (token == "\n" and depth):
            continuation.add(bisect.bisect_left(offsets, m.end()))
        elif len(token) > 1 and "\n" in token:
            literals.update(range(bisect.bisect_left(offsets, m.start()) + 1,
                                  bisect.bisect_left(offsets, m.end()) + 1))
    return literals, literals | continuation


def convert_py2_blocks(path, rel):
    with open(path, encoding="utf-8-sig", newline=None) as f:
        source = f.read()
    lines = source.splitlines(keepends=True)
    i = 0
    edits = []
    inside_string, continued = _logical_rows(source)
    if rel.lower().endswith("_ren.py"):
        fixed = convert_py2_block(source, rel)
        if fixed != source:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(fixed)
        return fixed != source
    while i < len(lines):
        line = lines[i].rstrip("\n")
        match = None if i in continued else _PYTHON_BLOCK.fullmatch(line)
        if match is None:
            i += 1
            continue
        filename = "%s:%d" % (rel, i + 1)
        inline = match.group("inline")
        if inline is not None:
            end = i + 1
            while end < len(lines) and end in continued:
                end += 1
            statement = inline + "\n" + "".join(lines[i + 1:end])
            fixed = convert_py2_block(statement, filename)
            if fixed != statement:
                indent = match.group("indent")
                if "\n" in fixed.rstrip("\n"):
                    # Reindent relative to the first line's own margin, not a fixed 4:
                    # the statement may open a block that is itself indented.
                    margin = " " * (len(statement.splitlines()[0]) - len(statement.splitlines()[0].lstrip(" ")))
                    literal_rows, _ = _logical_rows(fixed)
                    replacement = indent + "python:\n" + "".join(
                        margin + text if j not in literal_rows and text.strip() else text
                        for j, text in enumerate(fixed.splitlines(keepends=True)))
                else:
                    replacement = line[:match.start("inline")] + fixed
                edits.append((i, end, replacement))
            i = end
            continue
        indent = len(match.group("indent"))
        start = end = i + 1
        while end < len(lines):
            body_line = lines[end]
            if (end not in continued and body_line.strip() and not body_line.lstrip().startswith("#")
                    and len(body_line) - len(body_line.lstrip(" ")) <= indent):
                break
            end += 1
        while end > start and end - 1 not in inside_string and (
                not lines[end - 1].strip() or len(lines[end - 1]) - len(lines[end - 1].lstrip(" ")) <= indent):
            end -= 1
        margins = [len(lines[j]) - len(lines[j].lstrip(" ")) for j in range(start, end)
                   if j not in inside_string and lines[j].strip() and not lines[j].lstrip().startswith("#")]
        if margins:
            margin = " " * min(margins)
            dedented = "".join(lines[j].removeprefix(margin) if j not in inside_string else lines[j]
                               for j in range(start, end))
            fixed = convert_py2_block(dedented, filename)
            if fixed != dedented:
                literal_rows, _ = _logical_rows(fixed)
                replacement = "".join(margin + s if j not in literal_rows and s.strip() else s
                                      for j, s in enumerate(fixed.splitlines(keepends=True)))
                edits.append((start, end, replacement))
        i = end
    if edits:
        for start, end, replacement in reversed(edits):
            lines[start:end] = [replacement]
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.writelines(lines)
    return bool(edits)


def stage_py2_sources(gamedir, files, extra, workdir):
    """Convert staged copies only; export overrides for the files that changed."""
    out = dict(extra)
    changed = 0
    staged = tempfile.mkdtemp(prefix="python3-sources-", dir=workdir)
    for rel, _ in files:
        if rel.startswith("../") or not rel.lower().endswith((".rpy", ".rpym", "_ren.py")):
            continue
        src = out.get(rel) or os.path.join(gamedir, *rel.split("/"))
        dst = os.path.join(staged, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        did_change = convert_py2_blocks(dst, rel)
        if did_change:
            changed += 1
            out[rel] = dst
    return out, changed


def find_unrpyc():
    """How to run unrpyc: the exe embedded in the packaged program, or its source from
    fetch.sh run on the importer's own Python (3.9+); None when neither is there."""
    return bundled_tool("unrpyc") or (UNRPYC if os.path.isfile(UNRPYC) else None)


def decompile_scripts(gamedir, compiled, tool, workdir, warnings, rpycdec_python=None):
    """--decompile: rebuild the .rpy of compiled scripts with tool, "unrpyc" or "rpycdec"
    (DECOMPILERS). Both unpickle them, so they run in their own process on copies in
    workdir, never in the importer. A script the tool cannot decompile is refused, or under
    tl/ left out with a warning, as recompile_files() does with sourceless ones.
    rpycdec_python: an interpreter that has rpycdec, else rpycdec on PATH; the packaged
    program runs the rpycdec exe embedded in it (build_app.py).
    Returns {decompiled .rpy: path}."""
    work = tempfile.mkdtemp(prefix="decompile-", dir=workdir)
    copies = os.path.join(work, "game")
    env = None
    if tool == "unrpyc":
        unrpyc = find_unrpyc()
        if not unrpyc:
            raise Refused("--decompile：未找到 unrpyc；打包程序应自带它（重新构建时请先运行 "
                          "third_party/unrpyc/fetch.sh），从源码运行时同样需要 fetch.sh "
                          "获取，并用 Python 3.9+ 运行导入器")
        # --no-init-offset: its init offset guess is only safe for Ren'Py 8 (the output is the same game).
        # The embedded exe runs directly; the source runs on the importer's Python.
        cmd = [unrpyc, "--no-init-offset", copies] if unrpyc.endswith(".exe") \
            else [sys.executable, unrpyc, "--no-init-offset", copies]
    else:
        bundled = bundled_tool("rpycdec")
        cmd = [rpycdec_python, "-m", "rpycdec"] if rpycdec_python else [bundled or find_tool(("rpycdec",))]
        cmd += ["decompile", copies]
        if not cmd[0]:
            raise Refused("--decompile：未找到 rpycdec；打包程序应自带它（重新构建时请 "
                          "`pip install rpycdec`），从源码运行时也请安装它（需要 Python 3.10+），"
                          "或用 --rpycdec-python 指定其解释器")
        env = dict(os.environ, RPYCDEC_NO_WARNING="1")
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
        # rpycdec prints the path with either separator (it reports cwd-relative ones as
        # …/game\tl\…); normalize first, then strip the work folder.
        why = next((l for l in err if l.startswith("ERROR:")), err[-1])
        why = why.replace("\\", "/").replace(copies.replace("\\", "/") + "/", "")
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
    fixed = fix_empty_atl_blocks(out)
    if out:
        warnings.append("用 %s 反编译了 %d 个没有 .rpy 源文件的脚本（%s）。反编译结果可能与原文件不同："
                        "注释和格式会丢失，语句也可能被误读，请通玩整个游戏以确认"
                        % (tool, len(out), examples(out)))
    if fixed:
        warnings.append("%d 个反编译脚本的空 ATL 块已修复为 Ren'Py 8 语法（%s）；原游戏在 Ren'Py 6.x "
                        "接受空块" % (len(fixed), examples(fixed)))
    if partial:
        warnings.append("%s 无法反编译 %s 中的部分语句；这些语句以注释形式保留"
                        "（搜索 %r），游戏可能在该处出错" % (tool, examples(partial), mark))
    return out


EMPTY_ATL = re.compile(r"^(\s*(?:scene|show|hide|window|with|play|stop|queue|pause)\b[^:\n]*):$")
EMPTY_INIT = re.compile(r"^(?P<indent> *)init(?: +[-+]?\d+)? *: *(?:#.*)?$")


def fix_empty_atl_blocks(out):
    """Ren'Py 6.x allowed empty ATL and init blocks; 8.5.3 rejects them. A bare
    `scene bg:` is a plain scene statement if nothing is indented beneath it, so drop
    its colon. An empty `init -501:` keeps its priority but needs a `pass` body."""
    fixed = []
    for rel, path in out.items():
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            lines = f.read().splitlines()
        changed = False
        i = 0
        while i < len(lines):
            line = lines[i]
            atl = EMPTY_ATL.match(line)
            init = EMPTY_INIT.match(line)
            if not atl and not init:
                i += 1
                continue
            indent = len(line) - len(line.lstrip(" "))
            nxt = next((l for l in lines[i + 1:] if l.strip() and not l.lstrip().startswith("#")), "")
            empty = not nxt or len(nxt) - len(nxt.lstrip(" ")) <= indent
            if empty and atl:
                lines[i] = line[:-1]
                changed = True
            elif empty and init:
                lines.insert(i + 1, " " * (indent + 4) + "pass")
                changed = True
                i += 1
            i += 1
        if changed:
            with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
                f.write("\n".join(lines) + "\n")
            fixed.append(rel)
    return fixed
