#!/usr/bin/env python3
"""Builds the importer window as a program for people without Python (PyInstaller).

usage: python -m pip install pyinstaller pywebview -r importer/requirements.txt
       python importer/build_app.py

Windows: dist/importer/RSL Importer.exe (one file). macOS: dist/importer/RSL Importer.app.
Build on the system you build for; PyInstaller does not cross-compile. FFmpeg and 7-Zip
are not bundled (their licences); the program finds them on the user's system, see
docs/IMPORTER.md.

The decompilers ARE bundled: build_app.py fetches the pinned unrpyc source
(third_party/unrpyc/fetch.sh) and pip-installs rpycdec, builds each into its own
one-file executable, and embeds both; the program runs them as subprocesses on copies
of a game's scripts, exactly as it runs them from source. Whichever cannot be built
here is left out with a note, and the program reports it as missing at run time.
"""
import os
import subprocess
import sys
import tempfile

import PyInstaller.__main__

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(here)
WINDOWS = sys.platform == "win32"
DECOMPILERS = os.path.join(repo, "build", "decompilers")


def run(args, **kw):
    print("+ %s" % " ".join(args), flush=True)
    return subprocess.run(args, **kw)


def build_unrpyc():
    """The pinned unrpyc source (fetch.sh), built into build/decompilers/unrpyc(.exe)."""
    if run(["sh", os.path.join(repo, "third_party", "unrpyc", "fetch.sh")]).returncode:
        raise RuntimeError("fetch.sh failed")
    source = os.path.join(repo, "build", "unrpyc", "unrpyc", "unrpyc.py")
    if not os.path.isfile(source):
        raise RuntimeError("fetch.sh left no unrpyc.py")
    exe = os.path.join(DECOMPILERS, "unrpyc.exe" if WINDOWS else "unrpyc")
    build_onefile(source, exe, name="unrpyc")
    return exe


def build_rpycdec():
    """The pip package (pure stdlib at runtime), built into build/decompilers/rpycdec(.exe)."""
    if run([sys.executable, "-m", "pip", "install", "--quiet", "rpycdec"]).returncode:
        raise RuntimeError("pip install rpycdec failed")
    with tempfile.TemporaryDirectory(prefix="rpycdec-entry-") as tmp:
        entry = os.path.join(tmp, "rpycdec_main.py")
        with open(entry, "w", encoding="utf-8") as f:
            f.write("from rpycdec import main\nmain()\n")
        exe = os.path.join(DECOMPILERS, "rpycdec.exe" if WINDOWS else "rpycdec")
        build_onefile(entry, exe, name="rpycdec")
        return exe


def build_onefile(entry, exe, name):
    """One PyInstaller --onefile build of entry, output into build/decompilers/."""
    PyInstaller.__main__.run([
        entry,
        "--name", name,
        "--onefile",
        "--console",                       # the decompiler's output is parsed
        "--noconfirm",
        "--distpath", DECOMPILERS,
        "--workpath", os.path.join(repo, "build", "test", "pyinstaller", name),
        "--specpath", os.path.join(repo, "build", "test", "pyinstaller", name),
    ])
    if not os.path.isfile(exe):
        raise RuntimeError("PyInstaller left no %s" % exe)


def decompilers():
    """[unrpyc exe, rpycdec exe] as far as they could be built."""
    os.makedirs(DECOMPILERS, exist_ok=True)
    built = []
    for what, build in (("unrpyc", build_unrpyc), ("rpycdec", build_rpycdec)):
        try:
            built.append(build())
        except Exception as e:
            print("note: the packaged program will not include %s (%s)" % (what, e), flush=True)
    return built


args = [
    os.path.join(here, "rsl_import_gui.py"),
    "--name", "RSL Importer",
    "--add-data", os.path.join(here, "rsl_import_gui.html") + os.pathsep + ".",
    "--collect-all", "fissix",                                 # dynamic fixers and parser grammar
    "--windowed",                                              # no console window
    "--icon", os.path.join(here, "icon.ico" if WINDOWS else "icon.icns"),  # scripts/icons.sh
    "--onefile" if WINDOWS else "--onedir",    # macOS: a .app bundle
    "--noconfirm",
    "--distpath", os.path.join(repo, "dist", "importer"),
    "--workpath", os.path.join(repo, "build", "test", "pyinstaller"),
    "--specpath", os.path.join(repo, "build", "test", "pyinstaller"),
]
for exe in decompilers():
    # PyInstaller's --add-binary separator: ';' on Windows, ':' elsewhere.
    args.append("--add-binary=%s%s." % (exe, os.pathsep))

PyInstaller.__main__.run(args)
