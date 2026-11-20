# 构建状态

最后更新：2026-10-23

## 当前里程碑

| 里程碑 | 状态 |
|---|---|
| 1 工具链 | 代码完成，可构建。在 Switch 上：加载器和运行时 `.nro` 双向启动并互相链式加载（运行 1） |
| 2 输入系统 | 代码完成，主机端已测试。在 Switch 上：掌机控制可操作加载器菜单（运行 1）；游戏中十字键、A 和虚拟光标可选择按钮（运行 16、18）。重映射、配置、紧急组合键、触摸、底座模式和手柄重连：**硬件未验证** |
| 3 Python 运行时 | 静态 CPython 3.12.8 交叉构建；运行时 `.nro` 可链接。在 Switch 上自检 2 秒完成，66/66 项必需导入、线程和 `os.urandom` 通过；一项失败：`sdmc:` 上的非 ASCII 文件名（运行 4、20）。SD 卡端按 Shift-JIS 解析名称，低于 libnx 的层级：只有 UTF-8 字节同时是合法 Shift-JIS 的名称才能创建，且目录列表会漏掉所有非 ASCII 条目（[运行 20](tests/hardware/results/2026-10-20-run20.md)） |
| 4 Ren'Py 引擎 | Ren'Py 8.5.3 可在 Switch 上构建、链接和运行：The Question（运行 5），以及 Forbidden Fantasy——一个在主机上重新编译的 Ren'Py 7 游戏（运行 13）。退出返回加载器（运行 7）；三个游戏连续在同一进程运行（运行 12）。手柄桥接对官方引擎通过 26/26 项桌面检查。音乐和音效可播放，也包括 `.rpa` 内的（[运行 21](tests/hardware/results/2026-10-21-run21.md)）。存档、休眠和触摸：见里程碑 6。小程序模式曾导致整台主机崩溃（运行 8 和 26），因此（加载器和运行时）在该模式下拒绝启动游戏 |
| 5 导入器 | macOS 导入器：文件夹/ZIP/7z/RAR → 兼容性报告、`manifest.json`、SD 导出。主机端已测试（25 项测试）。其导出的 The Question 已在 Switch 上启动（运行 9） |
| 6 游戏性可靠性 | **完成**（运行 27）。在 Switch 上验证：退出并启动另一个游戏不再崩溃（一个卡住的 Ren'Py 音频解码线程，运行 22–24）；游戏错误显示系统错误对话框并返回游戏库（运行 25）；休眠与唤醒（运行 23–24）；网站链接和文件夹按钮显示提示而不是失败（运行 25）；十字键可以触及框重叠的按钮（运行 25）；The Question 使用 SDK 字体的偏好设置（运行 25）。仅在桌面测试：游戏菜单从其侧边菜单开始；触摸后没有光标。另经验证（运行 26）：游戏中使用的重映射（`input: confirm (X)`）、存档/读档回到同一行（`script.rpy:72`）、自定义屏幕到 Done、HOME。游戏内的紧急组合键也为加载器保存默认值；小程序模式（从相册启动加载器，曾在运行 8 和 26 导致整台主机崩溃）下拒绝启动游戏（[运行 27](tests/hardware/results/2026-10-21-run27.md)）。仍然仅在桌面：触摸后没有光标；完整的拒绝文本（状态缓冲区在运行 27 后已修复） |
| 7 游戏库 | LVGL 加载器在 Switch 上运行（首次硬件测试，2026-10-22）：启动后 80 ms 出现首帧，空闲时核心占用 1%，SoC 36-38 °C，帧耗时 11-17 ms。用户的评审带来了第二轮（测量字体确保没有内容被裁切、图标居中、更大的封面、手柄形状的测试屏幕、布局和强调色页面、图文并茂的游戏指南），0 警告构建（`.nro` 731 KB）并推送进行第二次硬件测试。封面（2026-10-22）：导入器可以生成（菜单美术图，或用 `--cover` 或在桌面窗口选择的图片），Switch 上的*更换图片*可将相册截图转为封面；先在主机上测试，然后在 Switch 上于[运行 28](tests/hardware/results/2026-10-23-run28.md)（2026-10-23）测试：封面和占位图、布局、详情页、图片页和保存的封面，以及游戏的启动和退出，全部 PASS。该次运行后用户放弃了电影感布局和界面背后的所有图片：在主机上，全屏图片上的动画每帧耗时 22-35 ms，相比之下 11-17 ms，用户不愿付出这个代价。横排布局采用了电影感布局的形态（封面沿底部，游戏名称在上方），因此游戏库现在有横排和网格（`.nro` 786 KB） |
| 传输（7 之后，8 之前） | **FTP 服务器已构建**（2026-10-25）：+ 菜单 → *通过 Wi-Fi 添加游戏*在页面打开期间运行加载器自带的服务器（`platform/transfer/ftpd.c`），只触及 `switch/renpy-loader/`，在 Switch 上有进度条（导入器用 `ALLO` 通告大小）；导入器的 FTP 导出、替换和删除在主机上对其通过（`tests/unit/test_ftpd.py`），并在 Switch 上于[运行 29](tests/hardware/results/2026-10-25-run29.md)（2026-10-25）测试：从命令行导入器发送、替换和删除一个游戏，Switch 的进度条、"已到达"提示和弹出框（前往游戏库）、传输中途按 B，全部 PASS；单个大文件上行 2.64 MB/s、下行 2.47 MB/s，由小文件组成的游戏 0.7-0.8 MB/s。未尝试：导入器窗口配合使用、无 Wi-Fi。之后，只有当该速度不够时才做 USB 传输（Switch 上用 libnx `usbComms`，桌面用 libusb；几十 MB/s，相比之下 Wi-Fi 约 1–2 MB/s）。MTP 无计划：macOS 没有 MTP 支持，而 haze 之类的 MTP 响应端是 GPL，会使 MIT 的加载器变成 GPL |
| 8 发布验证 | 未开始 |

**Ren'Py 8.5.3 在真机 Switch 上运行**（2026-10-18，运行 5）："The Question" 启动、
以 nouveau 上的 GLES 3.2 渲染、显示对话并正常退出。音频可播放（运行 21）。存档、休眠和
触摸尚未在硬件上验证。见
[tests/hardware/results](tests/hardware/results/)。

## 已实现

**工具链与构建**
- 固定版本、容器化的 Switch 构建（`scripts/build.sh`、`docker/Dockerfile`），在 Apple
  Silicon 上原生 arm64。

**加载器（`renpy-loader.nro`）**
- LVGL 9.6 图形界面（`app/ui/ui.c`，`docs/LOADER_UI.md`），绘制在 CPU 上、写入 libnx
  帧缓冲，且仅在有变化时绘制：
  - 两种布局的游戏库（横排、网格）、游戏详情、+ 菜单、小程序模式提示、
    手柄设置、手柄测试、诊断、游戏指南（首次启动显示）；
  - OLED 深色主题，粉色或蓝色强调色；主机自带字体和按钮图标；
  - 设置保存在 `config/loader.json`；
  - `loader.log` 中的 `ui` 行：启动时间、每 60 秒一条摘要（帧数、渲染时间、核心
    占用、SoC 温度、堆内存、封面数）、限速的警告、退出行。
- 诊断 → A 链式加载运行时的 Python 自检，之后返回加载器。
- 游戏库读取每个游戏的 `manifest.json`：
  - 显示 Ren'Py 版本、状态、运行时，以及该运行时是否已安装；
  - 启动会运行该运行时；
  - 拒绝 `unsupported` 的游戏和运行时缺失的游戏。

**输入管线**（libnx HID → 任天堂名称规范化器 → 映射引擎 → 逻辑操作）
- 16 个逻辑操作，带按下、长按和重复行为。
- 十字键和模拟导航，外加带死区、速度和加速度的虚拟光标。
- 触摸读取，信箱模式坐标映射。
- 预设：默认、左手镜像、单手（右手）。
- 重映射界面：按键捕获、冲突标记、重置、预设、全局和按游戏配置，保存时即时生效。
- 带版本的 JSON 配置，严格校验。保存是原子的（`.tmp` → `.bak` 交换），
  加载时回退 `.bak`，并拒绝保存冲突或不完整的映射。
- 紧急恢复组合键（按住 + 和 − 3 秒），任何映射都无法禁用。
- 焦点变化、唤醒、手柄连接/断开/类型变化时的输入重置。

**运行时（`renpy-runtime.nro`）**
- Ren'Py 8.5.3（官方源码，60 个原生模块，引擎 `.pyc` 和预编译的公共脚本在 RomFS 中）。
  游戏模式：`--game <folder>`。按游戏的存档（`saves/<id>`）、日志（`logs/<id>`）和
  手柄配置；GLES2 渲染器。见 `runtime/renpy/README.md`。
- 手柄桥接：逻辑操作 → Ren'Py 手柄事件或命名事件。虚拟光标以软件光标驱动 Ren'Py 的
  鼠标。Ren'Py 自己的 SDL 手柄支持已关闭。
- 静态 CPython 3.12.8（6 个小补丁，见 `runtime/python/README.md`），69 个内置扩展模块。
- 标准库编译为无源码的 `.pyc`，存于 RomFS。
- Python 运行在 8 MiB 栈的线程上，主线程持续处理小程序事件。
- `_rslnx` 模块提供内存和小程序信息；`--return` 链式加载回加载器。
- Ren'Py 7 兼容（`rsl_bridge`）：对 `script_version` ≤ 7.5.3/8.0.3 的游戏，切换语言不会
  重新执行 `define gui.*`。Ren'Py 8.5.3 会，这会抹掉游戏在 `init python` 中设置的 gui 值。
  - 测试：`tests/integration/renpy_desktop/compat.sh`。
- 30 fps 省电模式，全局（`config/power-saver`）或按游戏（游戏文件夹中的 `power-saver`
  文件）。
- Switch 键盘：完整的系统键盘（`_rslnx.keyboard`，libnx `swkbdShow`）。每个文本字段
  打开一次，带自己的文本框和字段的当前文本。
  - 确定将文字放入字段；不会有任何东西自行移动。
  - Y，或没有选中内容时的 A，会重新打开它。
  - 仅当一个屏幕完全没有按钮时，A 才按下回车（A 不能提交 Ren'Py 的输入）。
  - SDL 的内联键盘不再使用。
- B 在游戏中返回（回退）；在菜单中仍然关闭菜单。
- 按钮选择（`rsl_bridge`）：当出现带按钮的新屏幕且没有东西（或只有暂停）获得焦点时，
  选中其第一个按钮。例如：New Game、第一个选项、第一张卡片。
  - 只考虑内容屏幕的按钮（绝不考虑 `config.overlay_screens`，如快速菜单）。
  - 跳过对话和活动的文本字段。
  - 对每个游戏生效。
- 虚拟光标保持隐藏，直到摇杆移动；按下按键后再次隐藏。其图像
  （`runtime/renpy/make_cursor.py`）是白描边的黑色箭头、圆润的抗锯齿拐角，风格类似
  Bibata Modern Classic 但从零绘制（Bibata 是 GPL-3.0）。热点在黑色尖端（4, 3）。
- `tests/integration/renpy_desktop/compat.sh`：对合成游戏的 14 项检查，附 plain-8.5.3
  复现和模拟的 Switch 键盘。The Question 桌面场景：26 项检查（加入游戏中的 B）。

**导入器（`importer/rsl_import.py`，macOS，仅标准库）** — 见 `docs/IMPORTER.md`
- 输入：游戏文件夹、`.zip`，或经 `7z` 命令行的 `.7z`/`.rar`。
- 安全解压到临时文件夹：拒绝绝对路径、`..`、指向外部的链接、密码和解压炸弹
  （文件数、总大小和压缩率限制）。
- 检测游戏目录（Ren'Py 的 `path_to_gamedir` 规则）、Ren'Py 版本、Python 2 或 3、
  自定义 Python、Windows 原生代码、少见媒体和非 ASCII 名称。
- 写入 `games/<id>/game` + 带版本的 `manifest.json`（`experimental` 或 `unsupported`，
  绝不 `verified`）。没有 `--replace` 时拒绝覆盖，并保留存档。
- `--recompile`（实验）：带 `.rpy` 源码的 Ren'Py 7 游戏导出时不带 `.rpyc`，由 Ren'Py
  8.5.3 用 Python 3 编译。
- Switch 性能检查：
  - 高于 30 fps、高于 720p/1080p 或使用 HEVC/AV1 的视频；
  - 超出 4096 纹理上限的图片；
  - `--optimize` 将高于 30 fps 的视频重编码为 30 fps，尺寸和编解码器族不变（FFmpeg）。
  - 检查包括标准 `.rpa` 压缩包内的媒体。索引的读取不做反序列化。
  - 其他格式的压缩包报告为已锁定，不打开。
- 加载器用（`!`）标记有性能问题的游戏。

**其他**
- 逐行刷新的日志：`logs/loader.log`、`logs/runtime.log`。

## 编译结果（实际）

| 条目 | 结果 |
|---|---|
| 主机 | macOS 15.7.9，Apple Silicon（arm64），Colima 0.10.3（vz）+ Docker CLI 29.9.1 |
| 基础镜像 | `devkitpro/devkita64:20260219` @ `sha256:1fc388c3…0282`（arm64 变体 `645416b7…`） |
| 工具链 | devkitA64 r29.2，gcc 15.2.0，newlib 4.6.0.20260123，libnx 4.12.0，switch-tools 1.13.1 |
| 加载器构建 | 成功，0 警告（`-Wall -Wextra`） |
| CPython | `Python-3.12.8.tar.xz` sha256 `c909157b…154e`（python.org 的 MD5 与 renpy-build 副本一致） |
| CPython 构建 | `scripts/build.sh python`：configure + `libpython3.12.a` + 511 模块标准库 zip，成功 |
| 运行时构建 | 成功，0 警告，静态链接且无未解析符号 |
| Ren'Py 构建 | `scripts/build.sh renpy`：60 模块 / 73 对象入 `librenpy.a`；169 个引擎模块装配；63 个 SDK 公共脚本摘要校验 |
| `dist/renpy-runtime.nro` | 61,535,482 字节，sha256 `88c91423e7752b3e17e174fd2713ac13ef4c2dcf6fd1e3a9f8355e93751b831b`（运行时拆分，rpynx 标题） |
| `dist/renpy-loader.nro` | 818,694 字节，sha256 `27fd355ec1d773e8db319b44a80d24f3457cf684a0a62c843c37dd703d351256`（LVGL 加载器、封面、更换图片、rpynx 标题、运行 28 的标签修复，无电影感布局和界面背后的图片，首帧后逐帧解码封面，通过 Wi-Fi 添加游戏；运行 29 测试了它，包括弹出框；运行 28 本身测试的是 `19c2f0ed…`，其 2026-10-25 的后续是 `43398231…`，即注释改动前的同一份代码） |
| 可复现性 | 加载器：两次干净构建结果一致。里程碑 3 运行时：两次完整重建一致。Ren'Py 运行时未复查 |
| libnx 布局检查 | `input_switch.c` 中的 `_Static_assert` 通过 |

镜像中与里程碑 4 相关的其他软件包：switch-sdl2 2.28.5、switch-sdl2_image 2.0.4、
switch-sdl2_ttf 2.22.0、switch-sdl2_mixer 2.0.4、switch-mesa 20.1.0、switch-ffmpeg 7.1、
switch-freetype 2.13.3、switch-harfbuzz 10.0.1、switch-libfribidi 1.0.12、switch-libpng 1.6.48、
switch-libjpeg-turbo 2.1.2、switch-libwebp 1.0.2、switch-libavif 1.0.1、switch-zlib 1.3.1、
switch-bzip2 1.0.8、switch-liblzma 5.8.2。

## 测试结果（实际）

| 测试 | 环境 | 运行时 | 预期 | 实际 | 状态 |
|---|---|---|---|---|---|
| `scripts/test.sh`（151 项检查） | macOS 主机，Apple clang，ASan + UBSan | 不适用 | 全部通过 | 151/151 | PASS |
| `runtime/python/selftest.py` 仅对 `python312.zip` | Linux arm64 容器，主机 CPython 3.12.8 | CPython 3.12.8 | 0 项必需失败 | 0 失败（66 项必需检查） | PASS（仅主机） |
| `tests/unit/test_horizon_paths.py` | Linux arm64 容器，主机 CPython 3.12.8 | 打了补丁的 posixpath 和 importlib | 31 项检查 | 31 通过 | PASS（仅主机） |
| `tests/integration/renpy_desktop/run.sh` | Linux arm64 容器，Xvfb + Mesa llvmpipe 22.3.6，GLES 3.2 | **官方** Ren'Py 8.5.3 SDK + 我们的 `switch_main.py`/`rsl_bridge.py`，脚本化 `_rslnx` | 26 项检查 | 26 通过；截图在 `build/test/out/` | PASS（仅桌面） |
| `tests/unit/test_importer.py` + `test_importer_gui.py`（51 项测试，`scripts/test.sh` 的一部分） | macOS 主机，`/usr/bin/python3` 3.9.6 | 不适用 | 全部通过 | 51/51 | PASS |
| `tests/unit/test_ftpd.py`（4 项测试，`scripts/test.sh` 的一部分）：导入器驱动加载器的 FTP 服务器 | macOS 主机，Apple clang ASan + UBSan，`/usr/bin/python3` 3.9.6 | 不适用 | 全部通过 | 4/4 | PASS（仅主机） |

桌面运行在任何硬件测试之前发现并修复了两个桥接缺陷：
- 启动器模块被命名为 `renpy_*`，Ren'Py 启动时试图对它做序列化；
- 桥接事件名没有加入 `config.keymap`，Ren'Py 在鼠标事件上抛出
  "Invalid key specifier"。

主机端输入套件覆盖：
- 预设，以及任天堂 A/B 方向；
- npad 规范化器；
- 绑定/冲突/完整性；
- 按下/长按/重复计时（含停顿）；
- 导航摇杆死区；光标移动和加速度；
- 紧急组合键；唤醒后的重置；
- 摇杆和信箱模式数学；净化；
- JSON 往返加 11 个恶意输入；随仓库发布的配置；
- 原子保存，带中断写入和缺失文件回退。

自检运行证明了标准库 zip、无源码 `.pyc` 的 zipimport，以及测试本身。它不证明任何
关于 Horizon 的事情。

## 硬件验证

**首次主机运行，2026-10-18**（[tests/hardware/results/2026-10-18-run1.md](tests/hardware/results/2026-10-18-run1.md)）：
- PASS：加载器启动、目录、手柄检测、运行时已找到、双向链式加载、CPython 3.12.8 在
  主机上初始化。
- FAIL：自检无可见输出地停滞，Ren'Py 立即以状态 1 退出。两个错误都不可见，因为
  CPython 在 libnx 控制台上丢弃了 stdout/stderr；运行 2 前已修复。

**运行 2–3**（[运行 2](tests/hardware/results/2026-10-18-run2.md)、
[运行 3](tests/hardware/results/2026-10-18-run3.md)）：
- PASS：主机上全部 66 项必需的标准库导入。
- 找到的原因：
  - `select.poll()` 在 Horizon 上挂起；
  - importlib 自己的 `isabs` 破坏 `romfs:/` 路径。这就是挡住 Ren'Py 的原因。

  两者都在运行 4 前在运行时中修复（经 Wi-Fi FTP 推送）。

**运行 9，2026-10-19**（[tests/hardware/results/2026-10-19-run9.md](tests/hardware/results/2026-10-19-run9.md)）：
- PASS：导入器导出的 The Question（`games/the-question`）经 FTP 上传（173/173 个文件
  读回一致），从游戏库启动并以状态 0 退出。
- FAIL：返回 21 秒后，加载器的主菜单崩溃（"The software was closed because an error
  occurred"）。
  - 原因：运行时的 SDL 定时器线程比运行时活得久。`SDL_Quit()` 不会停止
    `SDL_AddTimer` 在没有 `SDL_INIT_TIMER` 时启动的定时器线程。
  - 运行时 `fed4dc1b…` 中的修复：`quit_sdl()` 接手定时器引用，让 SDL 加入该线程。
    运行 10：PASS，游戏之后加载器坚持了 76 秒。

**运行 10–19**（记录在[运行 9](tests/hardware/results/2026-10-19-run9.md)、
[运行 13](tests/hardware/results/2026-10-19-run13.md) 和
[运行 14](tests/hardware/results/2026-10-19-run14.md) 文件中）：
- PASS：运行时退出时关闭每个 pthread，因此三个游戏连续运行和退出到 Homebrew Menu
  在同一进程内进行而不再崩溃（运行 12）。
- PASS：Forbidden Fantasy（Ren'Py 7.5.3，用 `--recompile` 导出）在主机上编译其脚本并
  渲染（运行 13）。
- PASS：卡片和选项用十字键和 A 选择（运行 18）。运行 19 对按钮选择的修复已在主机上，
  但还没有硬件结果。

**硬件未验证**（任何运行中都没有结果）：
- 控制与主机：重映射、配置和紧急组合键（H9–H14、H35），触摸（H7、H29）、
  休眠/唤醒（H16、H25、H32）、手柄重连（H17）、底座模式（H18）；
- Ren'Py：存档/读档（H31）、流畅的视频播放（运行 13）、运行 8 崩溃后的
  小程序模式（H34）、完整的 Switch 键盘（运行 17 构建）。

清单：[docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)，H1–H56。

## 已知缺陷与限制

- 两个屏幕上标签被裁切而不是换行（运行 28）：游戏指南的一行和侧边菜单的副标题，都是
  固定宽度父容器内按内容宽度的标签。已在加载器中修复（`ui_pages.c`、`ui_library.c`），
  在主机上用比主机字体更宽的字体检查过，并于 2026-10-23 在主机上确认（运行 28，第二次
  会话）。`tests/ui/mockups.c` 现在会在换行标签超出父容器时失败。
- 封面解码慢：主机上四个封面 450-453 ms，约每个 113 ms（2026-10-25，两次启动）。它们
  每帧解码一个，因此游戏库出现后的几帧就要花这么久；四个游戏时使用中并未察觉，但 20
  个游戏的库按这个速度会花约 2 秒。[推测] 由导入器按绘制尺寸生成封面，或在 UI 线程之外
  解码，可以缩短。2026-10-23 之前第一个封面在第一帧解码，加载器启动要 226-260 ms；
  现在等待一帧，启动 86-94 ms（运行 28，后续）。
- 仍未在硬件上验证：底座模式下的加载器。运行 28 留下的其余项（封面框的触摸拖动、
  从布局页按 B、没有 `manifest.json` 的游戏）已于 2026-10-25 通过（运行 29，第四次
  会话）。
- 加载器运行期间，`logs/loader.log` 无法经加载器自己的 FTP 服务器读取：Switch 的文件
  系统对进程持有打开句柄的文件连 `stat` 都拒绝（运行 29）。之后经 sphaira 读取。
- 游戏内加载器覆盖层未构建；L 摇杆按下只显示提示。
- Live2D 和 3D 模型不受支持。
- SD 卡上的非 ASCII 文件名（运行 20）：约一半的日文名称无法创建，且目录列表中一个都
  不显示。导入器将这些文件打包进 `game/rsl-nonascii.rpa`；图像和声音可在 Switch 上从
  中加载（[运行 21](tests/hardware/results/2026-10-21-run21.md)）。带这类名称的脚本和
  压缩包保持散装，仍受影响。
- devkitPro 的 switch-ffmpeg 以 `--enable-gpl` 构建并静态链接，因此分发的
  `renpy-runtime.nro` 受 GPL（v2 或更新）约束：请随附其源码。以 LGPL 方式构建 FFmpeg
  需要自己的构建和重链接材料。
- 运行时 `.nro` 为 62 MB（61,535,482 字节）；尚无硬件上的内存或加载时间测量。
- 紧急组合键需要 + 和 − 同时按，因此单个分离的 Joy-Con 无法执行。
- 启动器 UI 状态机没有自动化测试（它需要 libnx）；其下的引擎有测试。
- 运行时自检从第二个线程写控制台文本。libnx 控制台不是线程安全的，文本可能撕裂
  （仅外观）。
- Python 缺少 `ctypes`（无 libffi）、`ssl`/`_hashlib`（无 OpenSSL）、`sqlite3`、`mmap`、
  子进程派生和 `multiprocessing`。需要这些的游戏会失败。见
  `runtime/python/README.md`。
- 链式加载需要加载器从 Homebrew Menu 启动。
- 许可证：项目源码为 MIT（`LICENSE`）；二进制见 README 的"法律"部分。
- 从游戏返回后崩溃（运行 9）：运行时的 SDL 定时器线程在运行时代码卸载后继续运行，
  然后使加载器崩溃。已在运行时 `fed4dc1b…` 修复，并在主机上确认（运行 10）。
- 游戏后退出到 Homebrew Menu 会使 sphaira 崩溃（运行 10）。
  - [可能的原因] libnx 没有 `pthread_detach`，CPython 已退出线程的栈保持映射。
  - 运行时 `35508472…` 中的修复（运行 11）：游戏后退出不再崩溃，6 个分离线程被关闭。
    某些会话中仍有 3 个线程栈（396 KiB）保持映射。该进程中的下一次启动随后在新 SDL
    表面上的 `memset` 中崩溃。
  - 运行时 `9914bf86…` 中的修复：跟踪每个 pthread，退出时关闭每个已退出的。运行 12
    通过：同一进程连续 3 个游戏加退出，`borrowed` 保持 8548 KiB，无崩溃。每游戏 3 个
    遗留是 Ren'Py 的 `ffmedia` 解码线程。
  - 变通：从 HOME 关闭软件。

## 后续工程任务

1. 在 Switch 上：底座模式，以及运行 29 留下的内容（导入器窗口、无 Wi-Fi），顺路拍摄
   指南所需的截图；然后据此在 `docs/` 中提供分步"游戏指南"。
2. 传输：只有当 0.7-0.8 MB/s（小文件组成的游戏，运行 29）确实太慢时才做 USB；决定它
  的是每文件开销，而不是 Wi-Fi。
3. 里程碑 8：发布验证。
