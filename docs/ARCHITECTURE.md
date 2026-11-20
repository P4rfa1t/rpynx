# 架构

## 分层

```
Switch 物理输入（libnx HID：npad 按键、模拟摇杆、触摸屏）
        │  platform/input/input_switch.c        唯一依赖 libnx 的输入文件
        ▼
设备输入规范化器  rsl_normalize_npad()  任天堂按键名，绝不用 SDL 手柄名
        ▼
手柄映射引擎  rsl_input_update()  绑定、死区、重复/长按、光标、紧急组合键
        ▼
逻辑操作（confirm、back、rollback、skip 等）
        ▼
使用者：加载器 UI，以及 Ren'Py 桥接（runtime/renpy/boot/rsl_bridge.py）
```

映射引擎（`platform/input/input.c`、`input_config.c`）和文件系统辅助函数
（`platform/filesystem/fs.c`）是不依赖 libnx 的可移植 C 代码，因此主机测试套件
（`scripts/test.sh`）运行的正是打进 `.nro` 的同一份代码。规范化器依赖的 libnx 位布局
由 `input_switch.c` 中的 `_Static_assert` 与 libnx 自身的常量核对，因此 libnx 的变动会
导致构建失败，而不是悄悄调换按键。

| 路径 | 作用 |
|---|---|
| `app/launcher/` | 加载器：`main.c` 启动、显示、主循环、`ui` 日志行；`actions.c` 输入与导航；`library.c` 游戏扫描、manifest、设置、启动；`controls.c` 重映射；`art.c` 更换图片、将相册截图作为封面（`launcher.h`、`loader.h`） |
| `app/ui/` | 加载器屏幕，libnx 帧缓冲上的 LVGL 9.6（`docs/LOADER_UI.md`）：`ui.c` 屏幕框架、构建和更新各屏幕；`ui_style.c` 字体、样式、强调色；`ui_widgets.c` 基础组件、触摸；`ui_art.c` 封面；`ui_library.c` 游戏库布局、+ 菜单；`ui_detail.c` 游戏详情；`ui_picture.c` 更换图片；`ui_controls.c` 手柄设置、测试、诊断；`ui_pages.c` 游戏指南与布局页面：布局和强调色（`ui_internal.h`） |
| `app/diagnostics/log.c` | 逐行刷新的日志，位于 `sdmc:/switch/renpy-loader/logs/loader.log`，另有内存中的末尾缓冲 |
| `platform/input/` | 规范化器、映射引擎、JSON 持久化、libnx 胶水 |
| `platform/filesystem/` | 带 `.bak` 回退的原子写入、有界读取、`mkdir -p` |
| `config/profiles/` | 手柄配置示例（与内置预设相同，由测试核对） |
| `third_party/cjson/` | cJSON 1.7.18，MIT，原样内置 |
| `third_party/lvgl/` | LVGL 9.6.0（MIT）：固定版本的获取脚本、`lv_conf.h`、库 Makefile；源码解压到 `build/lvgl/` |
| `tests/unit/` | 主机端单元测试 |
| `scripts/` | `build.sh`（固定版本的容器构建）、`test.sh`（主机端测试） |

| `runtime/main.c` | 运行时 `.nro`：内嵌 CPython，运行脚本（后续：Ren'Py 游戏） |
| `runtime/python/` | 静态 CPython 3.12.8 交叉构建：补丁、`config.site`、标准库打包器、自检（[README](../runtime/python/README.md)） |
| `runtime/renpy/` | Ren'Py 8.5.3 静态构建、引擎装配、启动脚本和手柄桥接（[README](../runtime/renpy/README.md)） |
| `runtime/compatibility/` | Switch 无法支持的特性的替换引擎模块（assimp） |
| `tests/integration/renpy_desktop/` | 桌面测试：官方 Ren'Py SDK + 我们的启动/桥接脚本化的手柄输入 |
| `docker/Dockerfile` | 固定版本的 devkitA64 镜像，外加构建主机端 CPython 所需的主机头文件 |

## 进程模型

加载器和运行时是两个独立的 `.nro` 文件。启动游戏时，加载器调用
libnx `envSetNextLoad(runtime, "runtime --return <loader> <args>")` 然后退出。Homebrew
加载器随后以全新进程启动运行时。运行时退出时调用
`envSetNextLoad(<loader>)`，将用户带回游戏库。

理由：
- 每个游戏都获得干净的进程和地址空间。SnepOMatic 报告过 Mesa/nouveau 在同一进程
  内初始化两次时的错误。
- 崩溃的游戏不会带走加载器的状态。
- 按游戏选择运行时只是 `runtimes/` 中另一个文件。

代价是每次启动多出几秒的 `.nro` 加载时间。尚未测量。

目标目录结构中的目录（`runtime/`、`importer/`、`platform/graphics` 等）在填充它们的
里程碑开始时才创建，不会提前。

## 关键决策

**输入不经过 SDL 的手柄层。** SDL 的 GameController API 按位置命名按键（Xbox 布局），
任天堂的 A 会变成 SDL 的 "B"。直接读取 libnx HID 并按任天堂名称映射，可以完全避开这种
调换。对里程碑 4 的影响：必须禁用 Ren'Py 自己的手柄处理（或不初始化 SDL 的摇杆子系统），
否则每次按键会被投递两次。桥接层会把逻辑操作转换为 Ren'Py 事件。

**手柄配置是带版本的 JSON，严格加载。** 未知的操作或按键、错误的类型、超出加载器理解的
版本，以及让必要操作（confirm、back、game_menu、overlay、up/down/left/right）没有绑定的
配置都会被拒绝。文件中缺失的操作保留默认值，因此以后新增操作无需迁移。被拒绝的文件回退
到 `<file>.bak`，再回退到默认值。保存时拒绝冲突或不完整的映射。

**紧急恢复组合键不可被重映射消除。** 同时按住 + 和 − 3 秒在映射之前的原始按键上检查。
它会在内存中恢复默认控制并打开手柄设置；由用户显式保存。限制：单个分离的 Joy-Con 只
有 + / − 中的一个，无法执行该组合键。

**原子写入。** Horizon 的 `rename` 不会替换已存在的文件，因此一次保存是：
写 `x.tmp` → fsync → 将 `x` 移为 `x.bak` → 将 `x.tmp` 移为 `x`。任何中断都会留下可读的
`x` 或 `x.bak`，且加载器在 `x` 缺失或损坏时读取 `x.bak`。

**唤醒 / 重连。** 在重新获得焦点、从休眠唤醒和手柄类型变化时，引擎会丢弃所有按住的
操作，并忽略仍然物理按下的按键，直到它们被松开。这避免了唤醒后卡住的跳过/回退和
重复输入。

## 固定版本

| 组件 | 版本 | 来源 |
|---|---|---|
| devkitA64 / libnx 容器 | `devkitpro/devkita64:20260219`，索引摘要 `sha256:1fc388c3…0282` | Docker Hub，arm64 + amd64 |
| cJSON | 1.7.18 | github.com/DaveGamble/cJSON 标签 v1.7.18 |
| Ren'Py 目标（第一阶段） | 8.5.3（最新稳定版，2026-05-16） | 计划中，里程碑 4 |
| CPython 目标 | 3.12.8（renpy-build 为 8.5.3 所用） | 计划中，里程碑 3 |

容器内的软件包版本由 `dkp-pacman -Q` 记录在 `BUILD_STATUS.md`。

## 运行时计划（里程碑 3–4），基于对现有移植的阅读

现有移植都不是干净的起点：

- **renpy-build 没有 Switch 目标。** 其 master 分支已迁往 SDL3，而 devkitPro 不打包
  SDL3；8.5.3 仍使用 SDL2 2.0.20，pygame_sdl2 合并进 `renpy.pygame`。
- **SnepOMatic/RenPy-Switch**（Ren'Py 8.5.3）是最新的移植，但没有许可证文件，其代码
  无法复用。它运行 CPython 3.13 而非 3.12，这迫使 Ren'Py 本体做出变通。其总体技术是
  公开知识，将独立重新实现：
  - 静态 CPython（`--disable-shared`，无 dlopen/mmap，`Setup.local` 将所有模块 `*static*`）；
  - Cython 生成的 Ren'Py 模块链接进同一个 ELF；
  - 带点分模块名的 `PyImport_ExtendInittab` 表；
  - switch-mesa GLES 上的 gl2。
- **jvrcruzGAMES/RenpySwitch** 没有许可证（上游 MIT 已撤下），每个 NRO 只有一个游戏，
  且存在 CPython 3.14 / 3.9 标准库不匹配的问题。
- **uyjulian/renpy-switch**（MIT，2022 年归档）面向 Ren'Py 7.3.5 / Python 2.7，是
  第三阶段的参考。

计划中来自 devkitPro portlibs 的依赖：switch-sdl2、switch-sdl2_image、switch-sdl2_ttf、
switch-mesa、switch-freetype、switch-harfbuzz、switch-libfribidi、switch-ffmpeg、
switch-libpng、switch-libjpeg-turbo、switch-libwebp、switch-zlib、switch-bzip2、
switch-liblzma。devkitPro 未打包、必须在此构建的：CPython、libffi（仅当需要 ctypes 时；
Ren'Py 核心并不需要它）。
