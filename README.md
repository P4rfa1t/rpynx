<p align="center">
  <img src="app/launcher/icon.jpg" width="160" alt="rpynx">
</p>

<h1 align="center">rpynx</h1>

<p align="center">
  <b>在 Switch 上玩 Ren'Py 游戏</b><br>
  <sub>非官方自制软件。与 Ren'Py 及任天堂无关联。</sub>
</p>

这是一个 Nintendo Switch 自制软件加载器（`.nro`），用于在 SD 卡上运行合法获得的、无 DRM 的
Ren'Py 游戏，并提供一流的可重映射手柄控制系统。

**状态：预览版。** Ren'Py 8.5.3 游戏可以在真机上运行：使用 GLES 3.2 渲染、播放音频、存档和读档，
并且可以正常休眠唤醒（硬件运行 5–27 号）。带封面的图形化游戏库（28 号）以及从导入器通过 Wi-Fi 发送游戏
（29 号）也已在 Switch 上测试。底座模式（Docked）游戏和手柄重新连接尚未测试。关于已构建、已测试和已通过
硬件验证的内容，请参阅 [BUILD_STATUS.md](BUILD_STATUS.md)。

## 下载

从[最新版本](https://github.com/P4rfa1t/rpynx/releases/latest)获取（本项目汉化版的仓库；上游原版见 [nixentric/rpynx](https://github.com/nixentric/rpynx)），无需自行构建。

| 文件 | 说明 |
|---|---|
| [renpy-loader.nro](https://github.com/P4rfa1t/rpynx/releases/latest/download/renpy-loader.nro) | 加载器：复制到 SD 卡的 `switch/renpy-loader/` 目录 |
| [renpy-runtime.nro](https://github.com/P4rfa1t/rpynx/releases/latest/download/renpy-runtime.nro) | Python 与 Ren'Py 运行时：复制到 `switch/renpy-loader/runtimes/` 目录 |
| [rpynx-importer-windows.exe](https://github.com/P4rfa1t/rpynx/releases/latest/download/rpynx-importer-windows.exe) | Windows 版导入器 |
| [rpynx-importer-macos-arm64.dmg](https://github.com/P4rfa1t/rpynx/releases/latest/download/rpynx-importer-macos-arm64.dmg) | Apple 芯片 Mac 版导入器：拖入「应用程序」即可 |
| [rpynx-importer-python.zip](https://github.com/P4rfa1t/rpynx/releases/latest/download/rpynx-importer-python.zip) | Python 版导入器，适用于 Linux 和 Intel Mac：`python3 rsl_import.py` |

每个版本都包含所有文件；未更改的部分沿用上一版本的文件，具体见版本说明。每个版本中的 `SHA256SUMS.txt`
包含校验和。导入器应用未签名：macOS 会要求确认（右键 → 打开），Windows 会显示 SmartScreen 提示（更多信息 →
仍要运行）。安装方法和导入游戏的步骤见：[rpynx.nixentric.com](https://rpynx.nixentric.com/getting-started/install/)。

版本由推送标签 `v<APP_VERSION>` 时触发的 [.github/workflows/release.yml](.github/workflows/release.yml) 生成。

## 快速上手

### 1. 安装（只需一次）

1. 一台可运行自制软件的 Switch：Atmosphère 和 Homebrew Menu。
2. 在 SD 卡上：将 `renpy-loader.nro` 复制到 `switch/renpy-loader/`，将 `renpy-runtime.nro` 复制到
   `switch/renpy-loader/runtimes/`。
3. 以**完整内存**启动 rpynx：在 HOME 菜单上，打开任意游戏时按住 **R**，保持按住直到出现 Homebrew Menu，
   然后打开 rpynx。从相册启动时内存不足，游戏无法启动。

### 2. 在电脑上导入游戏

开始之前：

- **无需解压游戏。** 将游戏文件夹，或下载下来的 `.zip`、`.7z` 或 `.rar` 文件交给导入器即可。它只提取
  Switch 所需的内容：`game/` 文件夹（脚本、图片、声音、`.rpa` 归档）。Windows、Mac 和 Linux 启动器、
  `renpy/` 和 `lib/` 会被留下，因为 Switch 运行时有自己的引擎。原始文件永远不会被修改。
- **可能需要的工具：** [7-Zip](https://www.7-zip.org/) 仅用于 `.7z` 和 `.rar`；
  [FFmpeg](https://ffmpeg.org/) 用于封面、视频检查和*优化*。文件夹或 `.zip` 两者都不需要。
- **优化**（`--optimize`）：当报告标记出帧率高于 30 fps 的视频时开启。它们会被重新编码为 30 fps，
  大小不变，从而在主机上流畅播放；其他内容不做改动。报告显示 `Switch performance: OK` 时无需开启。
- **Ren'Py 7** 游戏（Python 2）：开启*重新编译*（`--recompile`），让 Ren'Py 8.5.3 在主机上编译其脚本。

然后：

1. 打开导入器，将游戏文件夹或压缩包拖到其窗口中。阅读报告：状态以及需要注意的事项。
2. 选择存放位置：**SD 卡**，或**通过 Wi-Fi 导入到 Switch**。使用 Wi-Fi 时，在 Switch 上打开 rpynx，
   按 **+**，选择**通过 Wi-Fi 添加游戏**，然后输入屏幕上显示的地址。
3. **导出。** 通过 Wi-Fi 时，Switch 会显示进度条，然后显示「*标题* 已到达」。

命令行操作相同：

```bash
python3 rsl_import.py ~/Downloads/MyGame.zip --optimize --out ftp://192.168.1.20:5000
```

### 3. 游玩

1. 在游戏库中选择游戏，按 **A** 查看详情，然后点击**启动**。
2. **A** 推进剧情和做出选择，**B** 返回，**X** 隐藏文本框，**Y** 显示历史记录，**L**/**R** 回滚和前进，
   按住 **ZR** 跳过，**+** 打开游戏菜单以存档和读档。每个按键都可以重新映射（+ → *手柄设置*）。
3. 游戏菜单中的*退出*会返回游戏库。存档保存在 SD 卡上，每个游戏独立保存。
4. 按键失效了？同时按住 **+** 和 **−** 3 秒：默认控制将恢复。

完整指南：[rpynx.nixentric.com](https://rpynx.nixentric.com/)。

## 自行构建（macOS Apple Silicon、Linux 或带 WSL 2 的 Windows）

需要 Docker。在没有 Docker Desktop 的 macOS 上：

```bash
brew install colima docker
```

```bash
colima start --cpu 4 --memory 4 --vm-type vz
```

在 Windows 上，请在带 Ubuntu 的 WSL 2 中构建：脚本是 POSIX shell，需要 Linux 文件系统。Windows 上尚未
尝试过；相同步骤在 Linux 上可用。

```powershell
wsl --install -d Ubuntu
```

在 Ubuntu 终端中，Docker 可以来自 Windows 版 Docker Desktop（设置 → Resources → WSL integration：为
Ubuntu 开启），也可以来自 Ubuntu 本身：

```bash
sudo apt install docker.io git && sudo usermod -aG docker $USER
```

将仓库克隆到 Linux 主目录（`~/rpynx`），不要放在 `/mnt/c/` 下：那里的文件构建很慢，并且会丢失可执行权限。

然后：

```bash
scripts/build.sh
```

输出：`dist/renpy-loader.nro`。运行时（静态链接的 CPython；首次构建约 10 分钟）：

```bash
scripts/build.sh python
```

```bash
scripts/build.sh runtime
```

添加 Ren'Py 引擎（位于 `python` 与 `runtime` 之间；首次约 5 分钟）：

```bash
scripts/build.sh renpy
```

输出：`dist/renpy-runtime.nro`。桌面集成测试（官方 Ren'Py SDK + 我们的桥接层）：

```bash
tests/integration/renpy_desktop/run.sh
```

工具链使用固定版本的 `devkitpro/devkita64:20260219` 镜像（摘要见 `scripts/build.sh`）。不使用也不需要
任何 Nintendo SDK 文件。

主机单元测试（Apple clang 或 gcc，无需 Docker；其中的屏幕模拟图需要 macOS 的 `sips`）：

```bash
scripts/test.sh
```

## 文档

- [docs/INSTALL.md](docs/INSTALL.md)：在 Switch 上安装、SD 卡目录结构、控制方式
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：分层结构、设计决策、固定版本、运行时计划
- [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)：真机测试清单
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)：兼容性矩阵

## 关于本汉化版

这是 [nixentric/rpynx](https://github.com/nixentric/rpynx) 的非官方简体中文汉化版。汉化范围：

- **加载器界面**（`app/ui/`、`app/launcher/`、`runtime/main.c`）：全部用户可见文字。加载器在 Switch 上会尝试加载系统的简体中文共享字体（`PlSharedFontType_ChineseSimplified`）作为文字字体的回退，因此中文可以正常显示；游戏标题含中文时同样受益。
- **导入器**（`importer/`，命令行与桌面窗口）：全部用户可见文字，包括兼容性报告和警告。
- **文档**：README、`docs/` 全部文档与 `BUILD_STATUS.md`。

代码注释、日志（`loader.log`/`runtime.log`，便于搜索）与配置键名保持英文。与上游的差异只涉及字符串与文档；构建流程未改动。

---

## 法律声明

本仓库不包含任何游戏素材和任何 Nintendo SDK 材料，也不规避 DRM。请仅使用您拥有的、无 DRM 格式的游戏。

本项目自身的源代码采用 MIT 许可证（[LICENSE](LICENSE)）。

本仓库中的第三方代码：cJSON 1.7.18（MIT，`third_party/cjson/LICENSE`）。
LVGL 9.6.0（MIT）不存放在此处：`third_party/lvgl/fetch.sh` 会下载它，并进行校验和验证。

构建好的 `.nro` 文件还包含遵循其各自许可证的第三方代码，MIT 许可证不会替代这些许可证：
- `renpy-loader.nro`：libnx（ISC）、cJSON（MIT）、LVGL（MIT）及其自带的 stb_truetype
  （MIT 或公有领域）、TJpgDec（ChaN 的免费许可证）以及 Montserrat 字体（SIL OFL 1.1）。
  文本字体和按钮图标属于主机自身，运行时从其共享内存中读取；`.nro` 中不包含它们。
- `renpy-runtime.nro`：CPython（PSF）、Ren'Py（MIT，含 LGPL 衍生部分）、Ren'Py SDK 的字体
  （Source Han Sans Lite 和 Vazirmatn，SIL OFL 1.1）、
  SDL2（zlib）、Mesa（MIT）、libnx（ISC）等。
  - 它还静态链接了 devkitPro 的 switch-ffmpeg，该库**使用 `--enable-gpl` 构建**。
  - 因此整个运行时二进制文件受 **GPL（第 2 版或更高版本）** 约束。
  - 分发者必须提供完整的对应源代码，或书面提供源代码的获取方式。个人使用不受影响。每个版本的本仓库都会在运行时旁边附带
    FFmpeg 的源代码（`ffmpeg-7.1.tar.xz`，devkitPro 的 switch-ffmpeg 7.1）；其余部分为该版本标签对应的本仓库代码，
    在固定版本的 devkitPro 镜像中构建。
  - 若要改为 LGPL 许可，请在构建 FFmpeg 时不使用 `--enable-gpl`。由于是静态链接，仍需要提供 LGPL 重链接所需的材料。
