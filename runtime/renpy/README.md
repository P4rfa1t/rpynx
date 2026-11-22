# Nintendo Switch 上的 Ren’Py 8.5.3

**简体中文** · [English](README.en.md)

官方 Ren’Py 引擎与 `runtime/python` 的静态 CPython 一起嵌入 `dist/renpy-runtime.nro`。

```bash
scripts/build.sh python
scripts/build.sh renpy
scripts/build.sh runtime
```

## 固定输入

`build.sh` 检查 SHA-256。引擎源码为 `renpy-8.5.3-source.tar.bz2`（`vc_version` 8.5.3.26051504，摘要来自上游 PGP 签名的 `checksums.txt`）；ARM SDK `renpy-8.5.3-sdkarm.tar.bz2` 提供经过逐项摘要验证的 `renpy/common/*.rpyc`。Cython 3.2.4 使用 PyPI 源码包，以纯 Python 模式运行，版本对应上游 `uv.lock`。

## 构建与适配

1. 上游 `setup.py generate` 调用 Cython 生成 `tmp/gen3`。`hoststub/` 提供所需的最小 setuptools 接口。
2. `build_modules.py` 按上游 `src/Setup` 使用 devkitA64 编译原生模块，重命名 Cython 初始化函数并生成使用完整模块名的 inittab。产物为 `build/renpy/dist/lib/librenpy.a`。
3. `stage_engine.py` 将引擎和 `runtime/compatibility`、`runtime/renpy/boot` 放入 RomFS，生成 unchecked-hash `.pyc`，再加入与 `.rpy` 摘要匹配的公共 `.rpyc`，避免从只读 RomFS 每次重新编译。`make_cursor.py` 生成虚拟光标图片。

libhydrogen 使用基于 `randomGet` 的 `getentropy`。Switch 缺少 assimp，`renpy.gl2.assimp` 替身保留二维接口并为 `GLTFModel` 报错。专有 Live2D Cubism Core 对应的可选模块未构建。newlib 的 `posix_memalign` 由 `runtime/python/nx_shims.c` 补充，CPython 路径补丁支持 `sdmc:/` 和 `romfs:/`。上游 Ren’Py `.py` 文件保持原样。

## 第三方包与字体

SDK 编译的 CPython 3.12 `.pyc`（magic 3531）放入 `renpy-base`，供存档令牌、启动和下载模块使用。`ecdsa`、`six`、`urllib3` 为 MIT；`rsa`、`requests` 为 Apache-2.0；`pyasn1` 为 BSD-2-Clause；`idna`、`PySocks` 为 BSD-3-Clause；`chardet` 为 LGPL-2.1-or-later；`certifi` 为 MPL-2.0。分发前以各上游许可证为准。Switch 缺少 `ssl`，`requests` 无法访问 HTTPS。

`renpy-base/sdk-fonts` 包含 `SourceHanSansLite.ttf` 和 `Vazirmatn-RD-UI-Light.ttf`，均为 SIL OFL 1.1。SDK 示例通过 `config.renpy_base + "/sdk-fonts"` 查找字体，正式分发的游戏可在 `game/` 携带自己的副本。

## 启动和手柄桥接

`switch_main.py` 加载未改动的 `renpy.py`，安装桥接，日志写入 `sdmc:/switch/renpy-loader/logs/<id>`。运行时使用 `--savedir` 指定 `saves/<id>` 并选择 `gles2`。`rsl_bridge.py` 包装 `renpy.pygame.event.wait/poll`，空闲时每 16 ms 在引擎线程轮询控制器。`rsl_focus.py` 管理焦点，`rsl_keyboard.py` 接入系统键盘，`rsl_compat.py` 处理旧脚本、提示和音频退出兼容，`rsl_diag.py` 记录存档和读档。

| 默认操作 | Ren’Py 输入 |
| --- | --- |
| A、B、X、L、R、+、十字键与左摇杆方向 | `controller.post_event` 手柄事件，由游戏的 `config.pad_bindings` 决定。 |
| 长按 ZR／ZL | `skip`／`toggle_afm`。 |
| Y／−／左摇杆按下 | 由 `config.underlay` 键映射处理的 `rsl_*` 事件。 |
| 右摇杆移动、按下 | 更新鼠标位置并发送点击，显示软件光标。 |

关闭 Ren’Py 的 SDL 手柄处理，以避免同一次操作被处理两次。

## 验证与限制

历史交叉构建成功编译所有选中的原生模块。桌面集成记录为 24/24，通过 Linux ARM SDK、Xvfb、Mesa llvmpipe 与模拟 `_rslnx` 验证 The Question 的默认操作、存档和紧急提示；当前数量以 `scenario.py` 及实际输出为准。真机已经验证引擎启动、nouveau GLES 3.2、音视频及退出返回，覆盖情况见[构建状态](../../BUILD_STATUS.md)和[真机清单](../../docs/HARDWARE_TESTS.md)。

Live2D 与 3D 模型不受支持。游戏看到 Linux 和 `pc` 界面变体；缺失或过期的 `.rpyc` 会在游戏目录重新生成。左摇杆按下只显示浮层提示。十字键操作时软件光标会保留到 Ren’Py 的 30 秒隐藏超时。运行时静态链接启用 GPL 的 devkitPro `switch-ffmpeg` 7.1，因此组合二进制按 GPL 2 或以上分发；发布工作流提供对应源码。Ren’Py 自身的 MIT 和源自 LGPL 的部分仍适用。
