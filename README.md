# rpynx

**简体中文** · [English](README.en.md)

在支持自制软件的 Nintendo Switch 上原生运行无 DRM 的 Ren’Py 游戏。rpynx 提供基于 LVGL 的图形游戏库、可自定义的手柄控制映射、机内 ZIP 解压、自动海报封面提取、Wi-Fi FTP / USB 3.0 MTP 传输以及桌面导入工具。本项目为非官方实现，与 Nintendo 或 Ren’Py 无隶属关系。

## 当前实机状态

目前 **Ren’Py 8 与 Ren’Py 7 双原生运行时**均已在 Switch 实机通过启动、画面渲染、音频、存档读档、手柄/触屏操作与退出测试：

| 运行时 | 引擎与 Python 版本 | 适用游戏 | 实机验证记录 |
| --- | --- | --- | --- |
| `renpy-runtime.nro` | Ren’Py 8.5.3 + CPython 3.12.8 | Ren’Py 8.x 游戏 | `Z.A.T.O.`、`The Freak Circus`、`Prescription: LOVE` 等正常运行并退出 |
| `renpy-7.8.7.nro` | Ren’Py 7.8.7 + CPython 2.7.18 | Ren’Py 6.99 / 7.x 游戏 | `Doki Doki Literature Club!`、`Butterfly Soup`、`Our Life` 原生直启零报错运行 |

## 核心功能

- **双运行时原生直启**：加载器会自动识别游戏所需的引擎版本（6.99/7.x → `renpy-7.8.7.nro`，8.x → `renpy-runtime.nro`），无需对老游戏进行反编译或拆 `.rpa` 压缩包，即可保留原生 Python 2 逻辑与原版存档兼容性。
- **机内 ZIP 解压与识别**：直接将游戏目录或 `.zip` 压缩包放入 `sdmc:/switch/renpy-loader/games/`，启动时即可自动解压；同时支持无 `.zip` 后缀的 ZIP 文件头（`PK\x03\x04`）识别，以及自动将误放在根目录的 `.zip` 移入 `games/`。
- **2:3 海报式封面自动提取**：扫描游戏资源中的 `presplash` / 启动画面 / 标题图与标志，支持背景叠加与透明边缘裁切，生成全幅 2:3 游戏库封面；也可在详情页手动更换本地图片。
- **USB 3.0 MTP 与 Wi-Fi FTP 传输**：
  - 支持加载器内直接开启 **通过 USB 添加游戏**（MTP）或 **Wi-Fi FTP**；
  - MTP 已修复 Windows WPD 写入显示名时截断 `.zip` 后缀的问题，采用 4 MiB USB 分块、32 MiB 环形缓冲、FAT32 `ftruncate` 预分配以及独立的 Core 2 SD 写线程（注：Switch 默认仅启用 USB 2.0，需在 Atmosphere `/atmosphere/config/system_settings.ini` 中配置 `[usb] usb30_force_enabled = u8!0x1` 方可发挥 USB 3.0 速率）。
- **加载器与兼容优化**：
  - 使用 Switch 系统默认时钟频率，不进行强制锁频或超频；
  - 详情页支持 30fps 节能模式切换、更改封面、单游戏手柄映射设置以及按 `X` 删除游戏（保留 `saves/` 存档）；
  - 启动期内置无改动 `.rpy`/`.rpym` 写保护，防止翻译或脚本生成器每次启动刷写时间戳导致 `.rpyc` 重复全量编译；
  - 修复 Python 2.7 在 Horizon OS 下调用 `select(0)` 挂起、`waitpid`/`fcntl` 与 `_sysconfigdata` 缺失等兼容问题；
  - 加载器与桌面工具支持简体中文、繁体中文、英语界面。

## SD 卡安装与游玩

1. 从 [GitHub Releases](https://github.com/P4rfa1t/rpynx/releases) 下载（或按下方说明自行构建）三个 `.nro` 文件，按如下路径放入 Switch SD 卡：
   ```text
   sdmc:/switch/renpy-loader/
   ├── renpy-loader.nro
   └── runtimes/
       ├── renpy-runtime.nro      # Ren'Py 8.x 运行时
       └── renpy-7.8.7.nro        # Ren'Py 6.99 / 7.x 运行时
   ```
2. 在 Switch HOME 菜单按住 **R** 键启动任意已安装游戏进入 Homebrew Menu，再打开 `renpy-loader`。Ren’Py 需要完整内存环境，加载器会在相册（Applet）模式下拒绝启动游戏。
3. 将无 DRM 的 Ren’Py 游戏文件夹或 `.zip` 压缩包放入 `sdmc:/switch/renpy-loader/games/`（可通过读卡器、Wi-Fi FTP、USB MTP 或桌面导入器传输）。
4. 在游戏库选中游戏后按 **A** 进入详情页启动。

> **桌面导入器说明**：如果使用 [importer/](importer/) 打包工具，在已有 `renpy-7.8.7.nro` 的情况下，Ren’Py 7 游戏一般**无需勾选**“反编译”、“重新编译”或“解开 `.rpa`”；仅当需要将过高帧率视频压至 30fps，或为 Ren’Py 8.5.3 游戏在 Windows 上预编译 `.rpyc` 时才需开启对应选项。

### Switch HOME 桌面快捷方式（Forwarder）

支持直接**在加载器机内一键创建并安装到 Switch HOME 菜单**（无需 `prod.keys`，直接调用系统服务创建独立桌面图标；需主机已安装 sigpatches / sys-patch）：

1. 在游戏详情页点击 **“桌面快捷方式”** 按钮，加载器会自动裁切 `256×256` 封面图标、构建 Forwarder 并注册到 HOME 菜单；
2. 返回 Switch 主界面即可看到该游戏的独立图标，点击直接启动（天然为完整内存模式，退出游戏直接回到 HOME 菜单）；
3. 如需配合 Sphaira 等第三方工具手动制作，也可指向 `sdmc:/switch/renpy-loader/renpy-loader.nro` 并传入参数 `--launch <游戏目录名>`（附加 `--return-to-loader` 可在退出游戏时回到加载器）。

## 默认手柄按键

在加载器主界面的 **X / + 菜单** 或游戏详情页的 **手柄设置** 中可自定义全局与单游戏映射。默认布局如下：

| 按键 | 功能 | 说明 |
| --- | --- | --- |
| **A** | 确认 / 下一句 | 菜单确认、推进对话 |
| **B** | 返回 / 回退 | 菜单中返回上一级；对话中回退上一句 |
| **X** | 隐藏界面 | 隐藏/显示对话框 |
| **Y** | 对话历史 | 打开历史记录界面 |
| **L / R** | 回退 / 前进 | 往回或往前翻阅对话 |
| **ZL / ZR** | 自动播放 / 按住快进 | 切换自动模式 / 按住跳过 |
| **+ / -** | 游戏菜单 / 快捷栏 | 打开主/暂停菜单 / 切换快捷菜单 |
| **左摇杆 / 十字键** | 焦点导航 | 上下左右移动选中项 |
| **右摇杆 / 按下右摇杆** | 虚拟鼠标 / 左键点击 | 移动指针并点击屏幕元素 |

## 从源码构建

所有 Switch 产物均在固定工具链的 Docker 容器内编译（Windows 需开启 Docker Desktop / WSL 2）。在仓库根目录运行：

```bash
# 1. 构建图形加载器 -> dist/renpy-loader.nro
scripts/build.sh

# 2. 构建 Ren'Py 8 运行时 -> dist/renpy-runtime.nro
scripts/build.sh python
scripts/build.sh renpy
scripts/build.sh runtime

# 3. 构建 Ren'Py 7 运行时 -> dist/renpy-7.8.7.nro
scripts/build.sh python2
scripts/build.sh renpy7
scripts/build.sh runtime7
```

桌面导入器打包与依赖见 [docs/IMPORTER.md](docs/IMPORTER.md)，单元测试运行命令见 [AGENTS.md](AGENTS.md)。

## 文档索引

其余项目文档保持英文原始版本：

| 主题 | 文档 |
| --- | --- |
| 构建与验证记录 | [BUILD_STATUS.md](BUILD_STATUS.md)、[docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) |
| 架构与加载器界面 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)、[docs/LOADER_UI.md](docs/LOADER_UI.md) |
| 安装、导入器与兼容性 | [docs/INSTALL.md](docs/INSTALL.md)、[docs/IMPORTER.md](docs/IMPORTER.md)、[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) |
| 运行时构建细节 | [runtime/python/README.md](runtime/python/README.md)、[runtime/renpy/README.md](runtime/renpy/README.md) |
| 开发规范与测试 | [AGENTS.md](AGENTS.md) |

## 许可

本项目源代码采用 [MIT 许可证](LICENSE)，源自 [nixentric/rpynx](https://github.com/nixentric/rpynx)。第三方依赖遵循各自许可证；两个运行时 NRO 静态链接了启用 GPL 的 FFmpeg，对应源码与分发声明见 [发布工作流](.github/workflows/release.yml) 与 GitHub Release 说明。游戏资源由用户自行提供。
