# rpynx

**简体中文** · [English](README.en.md)

在支持自制软件的 Nintendo Switch 上运行无 DRM 的 Ren’Py 游戏。rpynx 提供图形游戏库、可重映射的手柄控制，以及用于准备和传输游戏的桌面导入器。这是非官方项目，与 Nintendo 或 Ren’Py 无隶属关系。

**预览阶段。** Ren’Py 8.5.3 已在 Switch 真机运行。游戏库、存档读档和 Wi-Fi FTP 都有真机测试记录。USB MTP 已进入源码并通过主机协议回归，Switch 实际 USB 传输仍待验证。详见 [构建与验证状态](BUILD_STATUS.md)。

## 安装与游玩

1. 从 [GitHub Releases](https://github.com/P4rfa1t/rpynx/releases) 查看可用产物，或按下方命令自行构建。将 `renpy-loader.nro` 放到 SD 卡的 `switch/renpy-loader/`，将 `renpy-runtime.nro` 放到其 `runtimes/` 子目录。
2. 在 HOME 菜单按住 **R** 启动已安装的游戏，直到显示 Homebrew Menu，再打开 rpynx。Ren’Py 游戏需要完整内存，加载器会在相册小程序模式下拒绝启动游戏。
3. 打开桌面导入器，选择游戏文件夹、ZIP、7z 或 RAR。选好解包、反编译等选项后点击 **分析**，查看报告，再选择目标并 **导出**。7z/RAR 需要 7-Zip，封面和视频处理需要 FFmpeg。
4. 在 Switch 游戏库选择游戏并启动。加载器的 **+** 菜单提供手柄设置，在游戏内可通过游戏菜单存档和读档。

导出可使用 SD 卡或加载器的 Wi-Fi FTP 页面。测试 USB MTP 时，在加载器的 **+** 菜单选择 **通过 USB 添加游戏**，用数据线连接，再从导入器的 **目标** 页面扫描设备。Windows 打包版使用 WPD，macOS/Linux 需要 `libmtp`。桌面和 Switch 界面随系统选择简体中文、繁体中文或英语，其他语言回退英语。

Ren’Py 7 及更早的 Python 2 游戏需要迁移，成功率取决于脚本和依赖。桌面预编译是显式选项，会执行游戏初始化代码，应只用于信任的游戏。具体步骤见 [安装指南](docs/INSTALL.md)、[导入器手册](docs/IMPORTER.md) 和 [兼容性说明](docs/COMPATIBILITY.md)。

## 从源码构建

在仓库根目录运行。Switch 构建需要正在运行的 Docker，可使用 Linux、macOS 或带 WSL 2 的 Windows。

```bash
scripts/build.sh
```

```bash
scripts/build.sh python
scripts/build.sh renpy
scripts/build.sh runtime
```

两个 NRO 位于 `dist/`。桌面打包和依赖见 [导入器手册](docs/IMPORTER.md)，测试命令见 [AGENTS.md](AGENTS.md)。

## 文档

| 主题 | 文档 |
| --- | --- |
| 项目进展与验证 | [构建状态](BUILD_STATUS.md)、[真机测试](docs/HARDWARE_TESTS.md) |
| 设计与界面 | [架构](docs/ARCHITECTURE.md)、[加载器界面](docs/LOADER_UI.md) |
| 运行时构建 | [CPython](runtime/python/README.md)、[Ren’Py](runtime/renpy/README.md) |
| 协作指南 | [AGENTS.md](AGENTS.md)、[CLAUDE.md](CLAUDE.md) |

## 许可

本项目源代码采用 [MIT 许可证](LICENSE)，源自 [nixentric/rpynx](https://github.com/nixentric/rpynx)。第三方组件遵循各自的许可证。运行时 NRO 链接启用 GPL 的 FFmpeg，其分发条款和对应源码见 [发布工作流](.github/workflows/release.yml) 与各版本说明。游戏内容由用户自行提供。
