"""Stage Ren'Py 7.8.7 and pygame_sdl2 Python code into RomFS for Switch.

Run with host Python 2.7.18 so compiled .pyc magic matches the Switch runtime.

usage: stage_engine.py <pygame_sdl2 dir> <renpy dir> <sdkarm dir> <out dir> <boot dir>
"""
from __future__ import print_function
import hashlib
import imp
import marshal
import os
import py_compile
import shutil
import struct
import sys

SKIP_EXT = {".pyx", ".pxd", ".pxi", ".pyi", ".c", ".h", ".o", ".pyc", ".pyo"}
ROMFS_BASE = "romfs:/renpy-base"
MAGIC = imp.get_magic()

SDK_PACKAGES = [
    "future", "past", "builtins", "copyreg", "html", "http", "reprlib",
    "socketserver", "ecdsa", "rsa", "pyasn1", "requests", "urllib3",
    "idna", "chardet", "certifi", "six.pyo", "socks.pyo", "typing.pyo",
]


def copy_tree(src, dst):
    for root, dirs, files in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__")
        rel = os.path.relpath(root, src)
        target_dir = dst if rel == "." else os.path.join(dst, rel)
        if not os.path.exists(target_dir):
            os.makedirs(target_dir)
        for f in sorted(files):
            if os.path.splitext(f)[1] not in SKIP_EXT:
                shutil.copy2(os.path.join(root, f), os.path.join(target_dir, f))


def copy_sdk_packages(sdk_py2, out):
    """Copy third-party pure-Python 2.7 packages from sdkarm, converting .pyo -> .pyc
    after verifying each code object can be unmarshaled by host Python 2.7.18."""
    copied = 0
    for item in SDK_PACKAGES:
        src = os.path.join(sdk_py2, item)
        if not os.path.exists(src):
            continue
        if os.path.isfile(src):
            dst_name = item[:-4] + ".pyc" if item.endswith(".pyo") else item
            dst = os.path.join(out, dst_name)
            with open(src, "rb") as fh:
                data = fh.read()
            if item.endswith(".pyo"):
                if data[:4] != MAGIC:
                    sys.exit("magic mismatch in %s" % src)
                marshal.loads(data[8:])
            with open(dst, "wb") as fh:
                fh.write(data)
            copied += 1
        else:
            for root, dirs, files in os.walk(src):
                dirs.sort()
                rel = os.path.relpath(root, sdk_py2)
                dst_dir = os.path.join(out, rel)
                if not os.path.exists(dst_dir):
                    os.makedirs(dst_dir)
                for f in sorted(files):
                    sp = os.path.join(root, f)
                    dp = os.path.join(dst_dir, f[:-4] + ".pyc" if f.endswith(".pyo") else f)
                    with open(sp, "rb") as fh:
                        data = fh.read()
                    if f.endswith(".pyo"):
                        if data[:4] != MAGIC:
                            sys.exit("magic mismatch in %s" % sp)
                        marshal.loads(data[8:])
                    with open(dp, "wb") as fh:
                        fh.write(data)
                    copied += 1
    return copied


def source_for(compiled):
    """common/x.rpyc -> x.rpy or x_ren.py; x.rpymc -> x.rpym."""
    base = compiled[:-1]
    if compiled.endswith(".rpyc") and not os.path.exists(base):
        return compiled[:-5] + "_ren.py"
    return base


def compile_py(src_path, cfile, dfile):
    with open(src_path, "rb") as f:
        source = f.read()
    # Normalize CRLF so Python 2.7 compile() never trips on Windows checkouts
    source = source.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    code = compile(source, dfile, "exec", dont_inherit=True)
    with open(cfile, "wb") as fc:
        fc.write(MAGIC)
        fc.write(struct.pack("<I", 0))
        marshal.dump(code, fc)


def main():
    pygame_dir, renpy_dir, sdkarm_dir, out, boot_dir = sys.argv[1:6]

    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)

    shutil.copy2(os.path.join(renpy_dir, "renpy.py"), out)
    copy_tree(os.path.join(renpy_dir, "renpy"), os.path.join(out, "renpy"))
    copy_tree(os.path.join(pygame_dir, "src", "pygame_sdl2"), os.path.join(out, "pygame_sdl2"))
    copy_tree(boot_dir, out)

    # Precompiled renpy/common/*.rpyc from sdkarm (Ren'Py 7.8.7 writes pure MD5 of source).
    sdk_common = os.path.join(sdkarm_dir, "renpy", "common")
    common = os.path.join(out, "renpy", "common")
    added = skipped = 0
    for root, dirs, files in os.walk(sdk_common):
        dirs.sort()
        for name in sorted(files):
            if not name.endswith((".rpyc", ".rpymc")):
                continue
            rel = os.path.relpath(os.path.join(root, name), sdk_common)
            ours = source_for(os.path.join(common, rel))
            if not os.path.exists(ours):
                sys.exit("SDK %s has no matching source in this Ren'Py tree" % rel)
            with open(ours, "rb") as f:
                want = hashlib.md5(f.read()).digest()
            with open(os.path.join(root, name), "rb") as f:
                data = f.read()
            if data[-16:] != want:
                print("skip %s: compiled from a different source" % rel)
                skipped += 1
                continue
            with open(os.path.join(common, rel), "wb") as f:
                f.write(data)
            added += 1

    # Third-party Python 2.7 packages & sdk-fonts from sdkarm
    sdk_copied = copy_sdk_packages(os.path.join(sdkarm_dir, "lib", "python2.7"), out)
    sdk_fonts = os.path.join(sdkarm_dir, "sdk-fonts")
    if os.path.isdir(sdk_fonts):
        shutil.copytree(sdk_fonts, os.path.join(out, "sdk-fonts"))

    # Compile all .py to .pyc next to each .py (Python 2.7 uses adjacent .pyc).
    compiled = 0
    for root, dirs, files in os.walk(out):
        dirs.sort()
        for f in sorted(files):
            if f.endswith(".py"):
                path = os.path.join(root, f)
                rel = os.path.relpath(path, out).replace(os.sep, "/")
                compile_py(path, path + "c", ROMFS_BASE + "/" + rel)
                compiled += 1

    print("%s: %d .py compiled, %d common scripts, %d sdk files" % (out, compiled, added, sdk_copied))


if __name__ == "__main__":
    main()
