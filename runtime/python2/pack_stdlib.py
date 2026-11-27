"""Compile the CPython 2.7 stdlib to .pyc and pack it into a zip for zipimport.

Must run under the same CPython version (host 2.7.18) so bytecode magic matches.
Output is deterministic: sorted entries, fixed timestamps.

usage: pack_stdlib.py <Lib dir> <out.zip> [extra.py ...]
"""
import imp
import marshal
import os
import struct
import sys
import zipfile

SKIP_DIRS = {
    "test", "idlelib", "lib-tk", "ensurepip", "lib2to3",
    "pydoc_data", "site-packages", "bsddb", "hotshot",
    "__phello__", "msilib", "compiler", "curses",
}
SKIP_PREFIXES = (
    "distutils/tests", "email/test", "json/tests",
    "sqlite3/test", "unittest/test", "ctypes/test", "lib2to3/tests",
)
MAGIC = imp.get_magic()


def pyc(source, path):
    code = compile(source, path, "exec", dont_inherit=True)
    return MAGIC + struct.pack("<I", 0) + marshal.dumps(code)


def add(zf, arcname, data):
    info = zipfile.ZipInfo(arcname, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def main():
    lib, out, extra = sys.argv[1], sys.argv[2], sys.argv[3:]
    entries = {}
    for root, dirs, files in os.walk(lib):
        rel_dir = os.path.relpath(root, lib).replace(os.sep, "/")
        if rel_dir != "." and any(rel_dir == p or rel_dir.startswith(p + "/") for p in SKIP_PREFIXES):
            dirs[:] = []
            continue
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("plat-"))
        for f in files:
            if f.endswith(".py"):
                path = os.path.join(root, f)
                rel = os.path.relpath(path, lib)
                entries[rel[:-3] + ".pyc"] = path
    for path in extra:
        entries[os.path.basename(path)[:-3] + ".pyc"] = path

    tmp = out + ".tmp"
    zf = zipfile.ZipFile(tmp, "w")
    try:
        for arcname in sorted(entries):
            fh = open(entries[arcname], "rb")
            try:
                source = fh.read()
            finally:
                fh.close()
            add(zf, arcname.replace(os.sep, "/"), pyc(source, arcname[:-1]))
    finally:
        zf.close()
    if os.path.exists(out):
        os.remove(out)
    os.rename(tmp, out)
    print("%s: %d modules" % (out, len(entries)))


if __name__ == "__main__":
    main()
