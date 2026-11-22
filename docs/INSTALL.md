# 安装

**简体中文** · [English](INSTALL.en.md)

需要一台能运行 homebrew 的 Switch（Atmosphère + Homebrew Menu）。

1. 将 `dist/renpy-loader.nro` 复制到 `sdmc:/switch/renpy-loader/renpy-loader.nro`。
   将 `dist/renpy-runtime.nro` 复制到 `sdmc:/switch/renpy-loader/runtimes/renpy-runtime.nro`。
2. 从 Homebrew Menu 启动它。首次启动时会创建以下内容：

```
sdmc:/switch/renpy-loader/
├── renpy-loader.nro
├── runtimes/renpy-runtime.nro   内嵌 Python（里程碑 4 起的 Ren'Py 引擎）
├── games/                 每个游戏一个文件夹（每个都必须包含 game/）
├── config/controller/
│   ├── global.json        保存手柄设置时写入
│   └── games/<name>.json  每个游戏的手柄配置
├── cache/                 临时空间（自检报告）
└── logs/                  loader.log、runtime.log
```

**内存：** 从相册启动时，homebrew 以 library applet 方式运行，内存有限。
Ren'Py 游戏需要完整的应用程序内存：启动已安装的游戏时按住 R 来启动 Homebrew Menu（标题覆盖）。
诊断界面会显示你处于哪种模式。**加载器不会在 applet 模式下启动游戏：** 在该模式下，游戏曾两次导致整台主机崩溃（硬件测试 8 和 26）。Python 自检仍会运行。

**Python 自检：** 诊断 → A 会启动运行时、运行 Python 自检，按 + 后返回加载器。这需要从 Homebrew Menu 启动加载器，因为链式加载会经过 Homebrew loader。

## 游戏

使用 macOS 导入工具（`importer/rsl_import.py`，参见 [IMPORTER.md](IMPORTER.md)）添加游戏。
它会检查无 DRM 的 Ren'Py 游戏，并将 `games/<id>/game/` 连同 `manifest.json` 导出到 SD 卡。Ren'Py 8 游戏标为 `experimental`（实验性）；Ren'Py 7 / Python 2 游戏标为 `unsupported`（不支持），除非使用 `--recompile` 导出。

游戏库会显示每个游戏的 Ren'Py 版本、状态和运行时，并用 `!` 标记存在 Switch 性能问题的游戏。启动会运行该游戏的运行时。加载器会拒绝 `unsupported` 游戏、缺少运行时的游戏，以及没有 `game/` 子文件夹的文件夹。
手动复制且没有 manifest 的游戏使用默认运行时。

## 操作

加载器自身的菜单使用与游戏相同的按键映射。默认值如下：

| 按钮 | 功能 | 按钮 | 功能 |
|---|---|---|---|
| A | 确认 / 下一步 | ZL | 切换自动前进 |
| B | 返回 / 取消 | ZR（按住） | 跳过 |
| X | 隐藏对话界面 | + | 游戏菜单 |
| Y | 历史 | − | 快速菜单 |
| L | 回滚 | 十字键 / 左摇杆 | 导航 |
| R | 前滚 | 右摇杆 | 虚拟鼠标光标 |
| 左摇杆按下 | 加载器覆盖层（游戏内，尚未实现） | 右摇杆按下 | 鼠标点击 |

**手柄设置**（主菜单）：选择一个操作，按 A，然后按下新按钮。
冲突的分配会被标记，在解决前无法保存。Y 切换预设（默认、左手镜像、单手右手），X 恢复默认，+ 保存。死区、光标速度和加速度、重复计时、摇杆角色以及光标可见性位于底部。

**每个游戏的配置：** 在游戏库中选中一个游戏，按 X。

**紧急恢复：** 在任何地方同时按住 **+ 和 − 3 秒**。默认控制会立即恢复。在加载器中会打开设置界面；按 + 保存。在游戏中，设置会立即保存到该游戏的配置和加载器的 `global.json`，因此加载器也会使用它们；每个旧文件都保留为 `.bak`。没有任何设置可以禁用此功能。单独一只脱离的 Joy-Con 只有 + / − 之一，因此无法执行此操作。

配置为纯 JSON（格式见 `config/profiles/default.json`）。损坏的文件会被忽略：加载器会回退到上一版本（`.bak`），再回退到默认值。
