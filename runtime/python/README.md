# Nintendo Switch 上的 CPython 3.12.8

**简体中文** · [English](README.en.md)

`runtime/main.c` 将面向 `aarch64-none-elf`（devkitA64 newlib + libnx）的静态 CPython 嵌入 `dist/renpy-runtime.nro`。3.12.8 与 Ren’Py 8.5.3 所用的 renpy-build 版本一致。

```bash
scripts/build.sh python
```

```bash
scripts/build.sh runtime
```

容器内首先构建同版本的主机解释器，缓存于 `build/python/host`，随后解包源码并应用 `patches/*.diff`。构建使用 `config.site`、`MODULE_BUILDTYPE=static`、`--disable-shared` 和 `switchvars.sh` 中的 portlib 环境。`libpython3.12.a` 合并 libmpdec、expat 和 HACL SHA-2。`pack_stdlib.py` 用主机解释器生成基于哈希、无源码的 `.pyc`，写入可复现的 `python312.zip`。头文件、静态库和标准库归档位于 `build/python/dist/`。

上游输入 `Python-3.12.8.tar.xz` 的 SHA-256 为 `c909157bb25ec114e5869124cc2a9c4a4d4c1e957ca4ff553f1edc692101154e`，对应上游 MD5 `d46e5bf9f2e596a3ba45fc0b3c053dd2`。

## 上游补丁

| 补丁 | 作用 |
| --- | --- |
| `0001-configure-horizon-host.diff` | 将 `aarch64-*-elf` 识别为 Horizon。 |
| `0002-posixmodule-statvfs-without-fstatvfs.diff` | 允许 newlib 在只有 `statvfs` 时查询磁盘空间。 |
| `0003-timemodule-newlib-timezone.diff` | 使用 newlib 的 `_timezone` 和 `_daylight`。 |
| `0004-subprocess-horizon-no-fork.diff` | 允许导入 `subprocess`；在调用无法执行的 `Popen` 时抛出异常。 |
| `0005-posixpath-horizon-device-paths.diff` | `os.path` 将 `sdmc:/`、`romfs:/` 识别为绝对路径。 |
| `0006-importlib-horizon-device-paths.diff` | importlib 的 `_path_isabs` 使用相同规则，修复真机运行 3 中发现的问题。 |

`config.site` 标注 `dlopen`、rlimit API、`chroot`、`fchdir`、`fdatasync`、`setgroups` 和 `_posixsubprocess` 不可用，并禁用无套接字服务时可能阻塞的 `poll`。`build.sh` 为 newlib 补定义 `SSIZE_MAX`。`nx_shims.c` 实现 `_getentropy_r`，通过 libnx `randomGet()` 为解释器哈希和 `os.urandom` 提供随机数据。配置参考 PyNX 和 SnepOMatic/RenPy-Switch，补丁独立实现。

## 原生模块

内置清单来自 `Modules/config.c`：

```text
array _asyncio _bisect _contextvars _csv _heapq _json _lsprof _opcode _pickle
_queue _random _struct _zoneinfo audioop math cmath _statistics _datetime _decimal
binascii _bz2 _lzma zlib _md5 _sha1 _sha2 _sha3 _blake2 pyexpat _elementtree
_codecs_{cn,hk,iso2022,jp,kr,tw} _multibytecodec unicodedata fcntl select _socket
_multiprocessing atexit faulthandler posix _signal _tracemalloc _codecs _collections
errno _io itertools _sre _thread time _typing _weakref _abc _functools _locale
_operator _stat _symtable _imp _ast _tokenize gc _string
```

`runtime/rslnx.c` 另提供 `memory()`、`applet_type()`、`input_poll()`、`cursor_visible()`、`wait_event()` 和 `keyboard()`。`ctypes`、SSL/HTTPS、SQLite、`mmap` 以及 `resource`、`grp`、`pwd`、`termios`、`readline`、`_curses` 不可用。`hashlib` 可用内置哈希。`_multiprocessing` 已构建，但系统没有 `fork`，无法使用多进程。

## 验证

历史交叉构建无未解析符号。同版本 Linux arm64 主机仅使用 `python312.zip` 时通过 `selftest.py`，验证了归档。Switch 自检通过启动、导入、线程、随机数、时间、内存和 `sdmc:` 文件系统检查，详见[真机清单](../../docs/HARDWARE_TESTS.md)。[运行 20](../../tests/hardware/results/2026-10-20-run20.md) 记录 SD 驱动按 Shift-JIS 解释名称，目录列表可能漏掉非 ASCII 文件。导入器为符合条件的资源生成 `rsl-nonascii.rpa`。
