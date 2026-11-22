#!/usr/bin/env python3
"""rpynx importer (Milestone 5).

Analyses a DRM-free Ren'Py game (folder, .zip, .7z or .rar), writes a compatibility
report and, with --out, an SD-ready tree:

    <out>/switch/renpy-loader/games/<id>/game/...
    <out>/switch/renpy-loader/games/<id>/manifest.json

--out ftp://HOST:PORT uploads the same tree to the Switch over FTP: the loader's own server
(+ > Add games over Wi-Fi) or sphaira's.

Standard library only; runs on Python 3.9+ on Windows, macOS (/usr/bin/python3) and
Linux. This file is the command line; the code is the rsl/ package next to it (rsl/__init__.py
lists its parts), and importer/rsl_import_gui.py is a window over the same package. It never executes
game code, never unpickles anything and never writes to the source. The optional --decompile
hands copies of .rpyc files to unrpyc, run in its own process. See docs/IMPORTER.md.

usage: python3 importer/rsl_import.py SOURCE [--out DIR] [--id ID] [--replace] [--cover IMAGE]
"""
from rsl.i18n import tr
import argparse
import sys
import tempfile

from rsl import DECOMPILERS, ERRORS, export, ID_MAX, load, make_art, make_id, report, rmtree, TOOL_DIRS


def main(argv=None):
    ap = argparse.ArgumentParser(description=tr("分析 Ren'Py 游戏并导出，供 Switch 上的 rpynx 运行。"))
    ap.add_argument("source", nargs="?", help=tr("游戏文件夹、.zip、.7z 或 .rar"))
    ap.add_argument("--mtp-list", action="store_true", help=tr("列出已连接的 rpynx USB MTP 导出目标"))
    ap.add_argument("--out", help=tr("导出根目录，如 SD 卡；经 FTP 则为 ftp://主机:端口（加载器的"
                                  "“经 Wi-Fi 添加游戏”，或 sphaira）；写入 switch/renpy-loader/games/<id>"))
    ap.add_argument("--id", help=tr("游戏 ID（文件夹名）；默认由标题生成"))
    ap.add_argument("--replace", action="store_true", help=tr("更新已有的导出（保留存档）"))
    ap.add_argument("--optimize", action="store_true",
                    help=tr("将高于 30 fps 的视频重编码为 30 fps（尺寸不变，需要 FFmpeg）"))
    ap.add_argument("--recompile", action="store_true",
                    help=tr("自带 .rpy 源码的 Python 2 游戏：导出时不带 .rpyc，由 Ren'Py 8 重新编译"))
    ap.add_argument("--decompile", action="store_true",
                    help=tr("同 --recompile，并先用 --decompiler 将没有 .rpy 的 .rpyc 重建出 .rpy，"
                         "反编译器作为独立进程运行"))
    ap.add_argument("--decompiler", choices=DECOMPILERS,
                    help=tr("配合 --decompile：unrpyc（默认；third_party/unrpyc/fetch.sh）或 rpycdec "
                         "（pip install rpycdec，Python 3.10+）；两者对比见 docs/IMPORTER.md"))
    ap.add_argument("--rpycdec-python", metavar="PYTHON",
                    help=tr("装有 rpycdec 的 Python 3.10+ 解释器；默认为 PATH 上的 rpycdec"))
    ap.add_argument("--unrpa", action="store_true",
                    help=tr("解开游戏的 .rpa 压缩包成散文件（导出到暂存区，源目录不变）：脚本在压缩包内的"
                         " Python 2 游戏（如 Steam 版 DDLC）借此可反编译并重新编译；导出不再含 .rpa"))
    ap.add_argument("--precompile", action="store_true",
                    help=tr("Windows：下载并使用 Ren'Py 8.5.3 在电脑上预编译脚本。会以你的权限运行游戏初始化代码，只对信任的游戏使用。"))
    ap.add_argument("--cover", metavar="IMAGE",
                    help=tr("游戏在加载器中的封面，FFmpeg 能读取的任意图片（2:3 海报比例最合适；"
                         "其他比例会从中间裁剪）；默认：其菜单立绘的中间部分。"
                         "--replace 会保留之前选定的封面"))
    ap.add_argument("--tools", metavar="DIR", help=tr("存放 ffmpeg/ffprobe 或 7z 的文件夹（若不在 PATH 上）"))
    a = ap.parse_args(argv)
    if a.mtp_list:
        from rsl.mtp import devices

        try:
            for device in devices():
                print("%s\t%s" % (device["target"], device["name"]))
        except ERRORS as e:
            print(tr("错误：%s") % e, file=sys.stderr)
            return 1
        return 0
    if a.source is None:
        ap.error(tr("请指定游戏来源，或使用 --mtp-list 列出 USB 设备。"))
    if a.id is not None and make_id(a.id) != a.id:
        ap.error("--id must be lowercase a-z, 0-9 and '-', at most %d chars (try %r)" % (ID_MAX, make_id(a.id)))
    for stream in (sys.stdout, sys.stderr):   # a Windows code page cannot encode every title
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    if a.decompiler and not a.decompile:
        ap.error("--decompiler needs --decompile")
    if a.rpycdec_python and a.decompiler != "rpycdec":
        ap.error("--rpycdec-python needs --decompile --decompiler rpycdec")
    if a.tools:
        TOOL_DIRS[:] = [a.tools]

    def show(done, total, rel, note):
        if note:
            print("  %s: %s" % (note, rel), flush=True)

    tmp = tempfile.mkdtemp(prefix="rsl-import-")
    try:
        decompile = (a.decompiler or DECOMPILERS[0]) if a.decompile else None
        manifest, gamedir, files, extra = load(a.source, tmp, a.id, a.recompile, a.optimize, decompile,
                                               a.rpycdec_python, a.unrpa, precompile=a.precompile)
        art = make_art(manifest, gamedir, files, tmp, a.cover)
        print(report(manifest), flush=True)
        if a.out:
            print(tr("已导出到 %s") % export(manifest, gamedir, files, a.out, a.replace, extra, show, art))
        return 0
    except ERRORS as e:
        print(tr("错误：%s") % e, file=sys.stderr)
        return 1
    finally:
        rmtree(tmp)


if __name__ == "__main__":
    sys.exit(main())
