"""Unpacking a .zip, .7z or .rar safely: limits, path checks, no links."""
import os
import posixpath
import re
import shutil
import stat
import zipfile
import zlib

from .common import Refused
from .tools import run_7z
from . import tools


# Archive limits. A source over any of them is refused before anything is written.
MAX_FILES = 200000
MAX_TOTAL_BYTES = 32 << 30      # uncompressed; large visual novels reach ~20 GB
MAX_RATIO = 200                 # uncompressed/compressed, per entry
RATIO_MIN_BYTES = 1 << 20       # smaller entries may compress as well as they like

# Names Windows cannot create as given: device names (CON, NUL.txt, ...), alternate data
# streams (a:b), and trailing dots or spaces, which it strips, so ".. " would become "..".
WINDOWS_BAD_NAME = re.compile(r'[<>:"|?*\x00-\x1f]|[. ]$|^(con|prn|aux|nul|com\d|lpt\d)(\.|$)', re.I)


def member_parts(name):
    """Validated path components of an archive member, [] for the root."""
    p = name.replace("\\", "/")
    if p.startswith("/") or re.match(r"[A-Za-z]:", p) or "\0" in p:
        raise Refused("压缩包条目含绝对路径：%r" % name)
    parts = [x for x in p.split("/") if x not in ("", ".")]
    if ".." in parts:
        raise Refused("压缩包条目逃逸出压缩包（..）：%r" % name)
    if tools.WINDOWS and any(WINDOWS_BAD_NAME.search(x) for x in parts):
        raise Refused("压缩包条目的名称在 Windows 上无法安全创建：%r" % name)
    return parts


def check_sizes(count, total):
    if count > MAX_FILES:
        raise Refused("压缩包有 %d 个条目（上限 %d）" % (count, MAX_FILES))
    if total > MAX_TOTAL_BYTES:
        raise Refused("压缩包解压后为 %d 字节（上限 %d）" % (total, MAX_TOTAL_BYTES))


def check_ratio(name, size, packed):
    if size > RATIO_MIN_BYTES and size > MAX_RATIO * max(packed, 1):
        raise Refused("压缩包条目 %r 压缩率为 %d:1（上限 %d:1），疑似解压炸弹，已拒绝"
                      % (name, size // max(packed, 1), MAX_RATIO))


def extract_zip(path, dest, warnings):
    with zipfile.ZipFile(path) as zf:
        infos = zf.infolist()
        check_sizes(len(infos), sum(i.file_size for i in infos))
        plan = []
        for i in infos:
            parts = member_parts(i.filename)
            if i.flag_bits & 1:
                raise Refused("压缩包有密码保护；请自行解压后导入该文件夹")
            check_ratio(i.filename, i.file_size, i.compress_size)
            if stat.S_ISLNK(i.external_attr >> 16):
                with zf.open(i) as f:
                    target = f.read(4096).decode("utf-8", "replace")
                where = posixpath.normpath(posixpath.join("/r", *parts[:-1], target.replace("\\", "/")))
                if target.startswith(("/", "\\")) or not where.startswith("/r/"):
                    raise Refused("压缩包符号链接 %r 指向压缩包之外（%r）" % (i.filename, target))
                warnings.append("跳过符号链接 %s（不会导出链接）" % i.filename)
                continue
            if parts:
                plan.append((i, parts))
        for i, parts in plan:
            out = os.path.join(dest, *parts)
            try:
                if i.is_dir():
                    os.makedirs(out, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with zf.open(i) as src, open(out, "xb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
            except FileExistsError:
                raise Refused("压缩包中 %r 出现两次（仅大小写不同？）" % i.filename)
            except (OSError, NotImplementedError, zipfile.BadZipFile, zlib.error) as e:
                raise Refused("无法解压 %r：%s" % (i.filename, e))


def extract_7z(path, dest, kind):
    path = os.path.abspath(path)
    # Dummy -p so an encrypted archive fails instead of prompting on the terminal.
    r = run_7z(["l", "-slt", "-p-", path])
    if r.returncode != 0:
        err = [l.split(" : ")[-1].strip() for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l]
        hint = "；若能在其他工具中打开，请在那里解压后导入该文件夹" if kind == "rar" else ""
        raise Refused("7z 无法打开 %s：%s%s" % (os.path.basename(path), err[0] if err else "exit %d" % r.returncode, hint))
    entries = []
    for block in r.stdout.split("\n----------\n", 1)[-1].split("\n\n"):
        e = dict(l.split(" = ", 1) for l in block.splitlines() if " = " in l)
        if "Path" in e:
            entries.append(e)
    total = 0
    for e in entries:
        member_parts(e["Path"])
        if e.get("Encrypted") == "+":
            raise Refused("压缩包有密码保护；请自行解压后导入该文件夹")
        if re.search(r"(^|\s)l[rwx-]{9}", e.get("Attributes", "")) or \
                any("Link" in k and v for k, v in e.items()):
            raise Refused("压缩包包含链接（%r）；请自行解压后导入该文件夹" % e["Path"])
        size = int(e.get("Size") or 0)
        total += size
        if e.get("Packed Size"):
            check_ratio(e["Path"], size, int(e["Packed Size"]))
    check_sizes(len(entries), total)
    check_ratio(os.path.basename(path), total, os.path.getsize(path))  # solid archives
    r = run_7z(["x", "-y", "-bd", "-p-", "-o" + dest, path])
    if r.returncode != 0:
        raise Refused("7z 解压 %s 失败：%s" % (os.path.basename(path), r.stderr.strip() or r.stdout[-300:]))


def check_tree(root):
    """After extraction: nothing may be a link, and the limits must still hold."""
    count = total = 0
    for dirpath, dirnames, filenames in os.walk(root):
        for n in dirnames + filenames:
            st = os.lstat(os.path.join(dirpath, n))
            if stat.S_ISLNK(st.st_mode):
                raise Refused("压缩包解出了符号链接：%s" % os.path.relpath(os.path.join(dirpath, n), root))
            if stat.S_ISREG(st.st_mode):
                if st.st_nlink > 1:
                    raise Refused("压缩包解出了硬链接：%s" % os.path.relpath(os.path.join(dirpath, n), root))
                count += 1
                total += st.st_size
    check_sizes(count, total)


def unpack(source, tmp, warnings):
    """Returns (root to analyse, source kind)."""
    if os.path.isdir(source):
        return source, "directory"
    low = source.lower()
    if low.endswith(".zip"):
        kind = "zip"
    elif low.endswith((".7z", ".rar")):
        kind = low.rsplit(".", 1)[1]
    else:
        raise Refused("不支持的来源 %r：请提供文件夹、.zip、.7z 或 .rar" % source)
    if not os.path.isfile(source):
        raise Refused("%s 不存在" % source)
    try:
        if kind == "zip":
            extract_zip(source, tmp, warnings)
        else:
            extract_7z(source, tmp, kind)
    except (zipfile.BadZipFile, zlib.error) as e:
        raise Refused("无法读取 %s：%s" % (os.path.basename(source), e))
    check_tree(tmp)
    return tmp, kind
