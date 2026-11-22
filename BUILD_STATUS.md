# 构建与验证状态

**简体中文** · [English](BUILD_STATUS.en.md) · [首页](README.md)

本页区分源码实现、主机测试和 Switch 真机结果。历史结果对应当时的构建，不能代替当前提交的验收。

| 模块 | 已有证据 | 待验证 |
| --- | --- | --- |
| 加载器 | LVGL 游戏库、封面、布局、详情和启动在 [运行 28](tests/hardware/results/2026-10-23-run28.md) 上通过。 | 新增多语言后的真机字形和布局。 |
| CPython / Ren’Py | CPython 3.12.8 自检见 [运行 4](tests/hardware/results/2026-10-18-run4.md)。Ren’Py 8.5.3 启动示例游戏见 [运行 5](tests/hardware/results/2026-10-18-run5.md)，音频见 [运行 21](tests/hardware/results/2026-10-21-run21.md)。 | 新版 NRO 的交叉构建与启动。 |
| 游戏输入与可靠性 | 重新映射和存档读档见 [运行 26](tests/hardware/results/2026-10-21-run26.md)。相册小程序模式拒绝启动游戏及退出行为见 [运行 27](tests/hardware/results/2026-10-21-run27.md)。 | 底座模式及控制器重连等覆盖见真机清单。 |
| 导入器 | `ac04b4f` 的 Windows 回归 70 项通过、2 项跳过。手动分析、系统语言、MTP 目标已进入源码。 | 新打包产物及各系统上的界面检查。 |
| Wi-Fi FTP | [运行 29](tests/hardware/results/2026-10-25-run29.md) 验证了命令行导入、替换、删除与传输中返回。 | 新版 GUI 连接的真机回归。 |
| USB MTP | Ubuntu 的 MTP/FTP 合计 8 项主机测试通过。Windows WPD DLL 已编译并通过无设备枚举测试。 | Switch USB 枚举、实际上传下载、拔线重连、替换保留存档与封面，以及 macOS/Linux 设备连接。 |
| GitHub Actions | 发布工作流已将 checkout、setup-python 和 artifact 操作升级到 Node.js 24 版本。 | 用新运行确认构建产物与 annotations。 |

## 已知限制

- Switch SD 卡上的非 ASCII 文件名可能无法正常列出，见 [运行 20](tests/hardware/results/2026-10-20-run20.md)。导入器可打包符合条件的资源，脚本和自定义加载行为仍需单独验证。
- 旧 Ren’Py 游戏的迁移处于实验阶段。真实 DDLC 压缩包已验证导出布局和 35 个反编译脚本，尚未用修复后的导出确认真机启动。预编译的首启耗时收益也待真机测量。
- MTP 单个对象上限接近 4 GiB。Windows 枚举及主机协议结果所用环境没有连接 Switch，当前版本尚未完成真机传输验收。

继续验证请使用 [真机测试清单](docs/HARDWARE_TESTS.md) 和 [兼容性说明](docs/COMPATIBILITY.md)。更早的里程碑细节可查 [重写前的构建状态](https://github.com/P4rfa1t/rpynx/blob/ac04b4f/BUILD_STATUS.md)。
