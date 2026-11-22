# rpynx 项目协作指南

**简体中文** · [English](AGENTS.en.md)

rpynx 是 Nintendo Switch 上的 Ren’Py 游戏加载器和运行时，另有桌面导入器。请从仓库根目录执行以下命令。

## 代码入口

- `app/launcher/` 管理游戏库、启动与传输会话，`app/ui/` 是 LVGL 界面。
- `runtime/` 嵌入 CPython 与 Ren’Py，`runtime/renpy/boot/` 包含运行时桥接与兼容逻辑。
- `platform/` 提供文件系统、输入与传输。MTP 协议核心与 Switch USB 服务分别位于 `platform/transfer/mtp_core.c` 和 `mtp_usb.c`。
- `importer/rsl_import.py` 和 `importer/rsl_import_gui.py` 是命令行和桌面入口。`importer/rsl/` 负责分析、归档、迁移、导出与游戏库操作，`importer/mtp_windows/` 提供 Windows WPD 适配器。
- `.github/workflows/release.yml` 定义发布构建和产物复用。架构、导入流程和真机用例见 [架构](docs/ARCHITECTURE.md)、[导入器](docs/IMPORTER.md)、[真机测试](docs/HARDWARE_TESTS.md)。

## 修改约定

- 兼容性修复以 Ren’Py 版本、脚本格式、资源布局或可识别的旧 API 行为为依据。游戏实例可用于复现和验证，补丁应适用于同类游戏。
- 导入默认只读取源游戏，转换和预编译在暂存副本上进行。GUI 只有用户点击「分析」才启动处理。来源或分析选项变化后，旧报告失效。桌面预编译需要显式选择，因为它会运行游戏初始化代码。
- 将游戏归档和编译脚本视为不可信数据。分析路径使用现有的成员名校验、大小限制和纯数据解析，避免在导入器进程中反序列化游戏 pickle。
- 保留 SD、FTP、MTP 的共用导出规则。替换和取消要覆盖存档、用户封面与中断后的恢复。MTP 服务只公开 `switch/renpy-loader` 库。
- 新增界面文字时同步检查简体中文、繁体中文与英语。桌面目录在 `importer/rsl/locale_*.py`，加载器目录在 `app/ui/locale_*.inc`，运行时提示在 `runtime/renpy/boot/rsl_locale.py`。保留格式参数、目录名、配置键和 manifest 字段的兼容性。游戏语言由游戏自身控制。

## 构建与验证

| 任务 | 命令 |
| --- | --- |
| 构建加载器 NRO | `scripts/build.sh` |
| 构建 Ren’Py 运行时 NRO | 依次执行 `scripts/build.sh python`、`scripts/build.sh renpy`、`scripts/build.sh runtime` |
| 桌面导入器回归 | `RSL_LANGUAGE=zh-CN python -m unittest tests.unit.test_importer tests.unit.test_importer_gui tests.unit.test_mtp` |
| 运行时兼容回归 | `python -m unittest tests.unit.test_compat_fixes` |
| 主机 C、FTP、MTP 与界面检查 | `scripts/test.sh`，其中界面模拟图需要 macOS 的 `sips` |
| 桌面 Ren’Py 集成 | `tests/integration/renpy_desktop/run.sh` |
| 打包桌面导入器 | 安装 `importer/requirements.txt` 与构建依赖后运行 `python importer/build_app.py`。Windows 原生 MTP 组件还需要 CMake 和 MSVC x64 工具链。 |

Switch 构建使用 Docker，产物位于 `dist/`。Python 测试可在 Windows 或 WSL 直接运行，POSIX FTP/MTP 测试需先构建对应的 `build/host/` 测试程序。主机测试、Switch 交叉构建和硬件测试分别报告。USB 枚举、断线恢复、游戏启动和首启耗时应在实际硬件上确认。发布 workflow 可手动运行以验证构建产物。发布标签会触发正式发布。
