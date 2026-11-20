# 导入工具

`importer/rsl_import.py` 会检查无 DRM 的 Ren'Py 游戏，并将其复制到 Switch 加载器所需的目录结构中。`importer/rsl_import_gui.py` 是同一代码的窗口版（见"桌面窗口"）。两者都只需要 Python 3.9+ 及其标准库，可在 Windows、macOS（命令行使用 `/usr/bin/python3`）或 Linux 上运行。系统中可选的两个工具会被自动查找（见"工具"）：
- 7-Zip，用于 `.7z` 和 `.rar` 文件；
- FFmpeg（`ffprobe`、`ffmpeg`），用于检查视频（`--optimize`），以及生成加载器显示的封面（见"封面"）。

没有 Python 的用户可以使用打包好的应用：`RSL Importer.exe` 或 `RSL Importer.app`（见"构建应用"）。

代码位于 `importer/rsl/` 包中；`rsl_import.py` 只是它的命令行，`rsl_import_gui.py` 是它的窗口。`rsl/__init__.py` 列出了两者都使用的内容。各模块按游戏经过的顺序排列：

| 模块 | 作用 |
|---|---|
| `archives` | 安全地解压 `.zip`、`.7z` 或 `.rar`（"压缩包安全"）。 |
| `analysis` | 查找游戏、其 Ren'Py 版本和 Python，运行检查，生成 manifest 和报告。 |
| `rpa` | 将标准 `.rpa` 索引作为普通数据读取；写入非 ASCII 压缩包。 |
| `media` | 判断什么会影响 Switch 加载（`--optimize`，"Switch 性能"），以及封面（"封面"）。 |
| `recompile` | `--recompile` 和 `--decompile`。 |
| `exporter` | 将游戏写入库中：SD 卡文件夹，或通过 FTP 上传到 Switch。 |
| `library` | 列出、删除和比较库中的内容。 |
| `tools` | 查找并运行 7-Zip、FFmpeg 和反编译器（"工具"）。 |
| `common` | 错误、ID 和小型辅助函数。 |

导入工具从不运行游戏代码，也从不反序列化任何内容。它从不修改源文件夹或压缩包，也不解密或解压 `.rpa` 压缩包。可选的 `--decompile` 会将 `.rpyc` 文件的副本交给外部反编译器（unrpyc 或 rpycdec），它们在各自的进程中运行（见"反编译"）。

## 用法

```bash
python3 importer/rsl_import.py SOURCE                    # 仅生成报告
python3 importer/rsl_import.py SOURCE --out /Volumes/SD  # 报告 + 导出（macOS）
py importer\rsl_import.py SOURCE --out E:\               # 报告 + 导出（Windows）
python3 importer/rsl_import.py SOURCE --out ftp://192.168.1.20:5000   # 报告 + 上传到 Switch
```

`SOURCE` 可以是：
- 游戏文件夹（发行版根目录、SDK 项目，或 `game/` 文件夹本身）；
- `.zip`、`.7z` 或 `.rar` 文件。

| 选项 | 含义 |
|---|---|
| `--out DIR` | 导出到 `DIR/switch/renpy-loader/games/<id>/`（`game/` + `manifest.json`）。`ftp://HOST:PORT` 会改为上传到 Switch（见"通过 FTP 上传到 Switch"）。 |
| `--id ID` | 自行选择文件夹名。它必须已是规范化格式（见下文）。 |
| `--replace` | 更新已有的导出。不加此选项时，如果 `games/<id>` 已存在，导入工具会拒绝。 |
| `--optimize` | 将高于 30 fps 的视频以 30 fps 重新编码，保持相同尺寸和恒定质量。需要 FFmpeg。见"Switch 性能"。 |
| `--cover IMAGE` | 加载器中显示的游戏封面，可使用 FFmpeg 能读取的任意图片（2:3 海报最合适）。默认：取游戏菜单美术图的中间部分。见"封面"。 |
| `--recompile` | 仅适用于 Ren'Py 7 / Python 2 游戏。导出时不包含 `.rpyc`，使 Ren'Py 8.5.3 用 Python 3 编译 `.rpy` 源文件（见下文）。 |
| `--decompile` | 为每个没有 `.rpy` 的 `.rpyc` 重建一个 `.rpy`，然后重新编译（隐含 `--recompile`）。见"反编译"。 |
| `--decompiler TOOL` | 与 `--decompile` 一起使用：`unrpyc`（默认）或 `rpycdec`。"反编译"中有两者的比较。 |
| `--tools DIR` | 包含 `ffmpeg`/`ffprobe` 或 `7z` 的文件夹，搜索时优先于 `PATH`（见"工具"）。 |
| `--rpycdec-python PYTHON` | 与 `--decompiler rpycdec` 一起使用：安装了 rpycdec 的 Python 3.10+ 解释器。默认：`PATH` 上的 `rpycdec` 命令。 |

退出状态：`0` 成功；`1` 被拒绝（压缩包不安全、不是 Ren'Py 游戏、游戏不受支持，或导出已存在）；`2` 参数错误。

示例，使用本仓库附带的 Ren'Py 演示游戏：

```bash
python3 importer/rsl_import.py build/test/sdk/the_question --out /tmp/sd
```

```text
The Question  (id: the-question)
  Ren'Py:  8.5.3.26051504   Python: 3
  Status:  EXPERIMENTAL on runtime renpy-8.5.3
```

## 桌面窗口

窗口是一个网页（`importer/rsl_import_gui.html`），由 pywebview 在系统自带的网页视图中显示：macOS 上为 WebKit，Windows 上为 Edge WebView2。该页面调用 `importer/rsl_import_gui.py` 中的 `Api` 类，而该类只调用 `rsl` 包；所有检查和复制都由导入工具完成。pywebview 是窗口唯一需要的包；命令行不需要任何额外包。

```bash
python3 -m pip install pywebview
```

```bash
python3 importer/rsl_import_gui.py
```

在 Windows 上：`py -m pip install pywebview`，然后 `py importer\rsl_import_gui.py`。或者使用打包好的应用，它完全不需要 Python（见"构建应用"）。

- **库**列出目标位置（`switch/renpy-loader/games/<id>/`）上已有的游戏，读取每个 `manifest.json`。点击卡片可查看报告和 *Delete…*（删除）（见"删除游戏"）。*Compare with folder…*（与文件夹比较）会将 PC 上的一组游戏与之比对（见"与一组游戏文件夹比较"）。状态标签含义如下：
  - *Experimental*（实验性）：没有警告，也没有 Switch 性能问题；
  - *Review*（需检查）：实验性，但有需要先阅读的警告或性能问题；
  - *Unsupported*（不支持）：manifest 的 `reasons` 说明原因；
  - *No manifest*（无清单）：手动复制，或不是由本导入工具导入的。

  每张卡片都会像加载器那样显示游戏封面（没有封面时显示占位字母；通过 Wi-Fi 时，封面会在网格之后逐个到达）。游戏卡片还会显示其封面，并带有 *Change cover…*（更换封面）：选择一张图片，它会被转换并立即以"所选图片"的形式写入 SD 卡或 Switch（见"封面"）。没有 `manifest.json` 的游戏无法设置图片。
- **导入**：
  1. 将游戏文件夹、`.zip`、`.7z` 或 `.rar` 拖到窗口上，或使用 *Choose folder…*（选择文件夹）/ *Choose archive…*（选择压缩包）。随即进行分析。
  2. 选项：*Optimize*（优化）对应 `--optimize`，*Recompile*（重新编译）对应 `--recompile`，*Decompile*（反编译）对应 `--decompile`，并可在其下选择反编译器（`--decompiler`；每个都会显示优缺点），*Game id*（游戏 ID）对应 `--id`（留空：由标题生成）。更改任一选项都会清除报告；请再次按 *Analyze*（分析）。当分析发现某个选项可以解决的问题时（帧率高于 30 fps 的视频；带有 `.rpy` 源文件的 Ren'Py 7 游戏；有未反编译脚本的游戏），该选项下方会出现一条提示，说明原因，其 *Turn on*（开启）按钮会勾选该选项并重新分析。这一判断由导入工具决定（`suggested_options`）。
  3. *Tools*（工具）显示是否找到了 7-Zip、`ffprobe`、`ffmpeg`、unrpyc 和 rpycdec。*Tools folder…*（工具文件夹）即 `--tools`。
  4. 报告与命令行的相同：状态、Ren'Py 版本、图片、警告、Switch 性能问题，以及 `Checked:`（检查）行（松散文件、压缩包内文件和加密的资源）。在其上方显示由游戏菜单美术图生成的封面，并带有 *Change cover…*（更换封面）（`--cover`）；所选图片会在导出时写入。
  5. *Replace*（替换）对应 `--replace`。*Export*（导出）逐个文件复制并显示进度条。重新编码视频可能需要几分钟，状态栏会对此进行说明。*Cancel*（取消）会在当前文件完成后停止：未完成的副本会被删除，已有的导出保持不变。导出期间关闭窗口也会执行同样的操作，然后程序退出。
- **目标**，即库的读取位置以及游戏的写入位置：
  - *SD card*（SD 卡）：选择其顶层文件夹，Windows 上为 `E:\`，macOS 上为 `/Volumes/<name>`。已挂载且包含 `switch/` 文件夹的驱动器会被列出，如果只有一个，则自动选中。
  - *Switch over Wi-Fi (FTP)*（通过 Wi-Fi 连接 Switch（FTP））：输入加载器"通过 Wi-Fi 添加游戏"页面（或 sphaira）显示的地址，例如 `192.168.1.20:5000`，然后按 *Connect*（连接）。库从 Switch 读取，*Export* 会以相同的进度条上传到 Switch（状态栏还会显示大文件已发送了多少），*Cancel* 会在当前文件内停止。

`unsupported` 游戏无法导出。`--rpycdec-python` 仅在命令行中可用。

来自游戏的文本（标题、报告、路径）会以纯文本形式放入页面，绝不作为 HTML：`options.rpy` 中的标题无法向可以导出的页面注入脚本。

在 macOS 自带的 `/usr/bin/python3`（3.9）上，`pip` 可能会选中已被撤回的 `pyobjc` 12.0 并尝试编译，这需要 Xcode 命令行工具。在这里它可以工作，但打包好的应用或 python.org 提供的 Python 3.10+ 可以避免这一问题。

## Windows 设置

1. 使用 `RSL Importer.exe`（无需 Python），或从 python.org 安装 Python 3.9+，然后运行 `py -m pip install pywebview`。窗口使用 Microsoft Edge WebView2，Windows 11 自带该组件；在 Windows 10 上，如果窗口无法打开，请从 Microsoft 安装"Evergreen" WebView2 运行时。
2. 7-Zip，从 7-zip.org 获取。它会在 `C:\Program Files\7-Zip` 中被自动找到，无需额外设置。
3. FFmpeg。运行 `winget install Gyan.FFmpeg` 会将其加入 `PATH`。或者解压从 ffmpeg.org 链接的构建版本，并用 *Tools folder…*（工具文件夹）选择其 `bin` 文件夹（`--tools C:\ffmpeg\bin`）。
4. 将 SD 卡插入电脑，并选择其盘符，例如 `E:\`。在命令行中，单独的 `--out E:` 同样表示该驱动器的顶层文件夹，而不是其当前文件夹。

Windows 上的不同之处：
- 无法以原名在 Windows 上创建的压缩包条目会被拒绝：设备名（`CON`、`NUL.txt`、`COM1` 等）、`:`（会写入备用数据流）、`<>"|?*`，以及以点或空格结尾的名称（Windows 会去掉这些字符，因此 `.. ` 会变成 `..`）。
- 7-Zip、FFmpeg、unrpyc 和 rpycdec 运行时不会弹出控制台窗口。
- 7-Zip 以 UTF-8 列出名称（`-sccUTF-8`），而不是使用控制台代码页。
- 压缩包中的只读文件会随临时文件夹一起删除。
- 控制台代码页无法显示的报告文本（例如重定向到 `cp1252` 控制台的日文标题）会以 `?` 显示，而不会让导入工具停止。
- 超过 260 个字符的路径需要 Windows 的长路径设置（`LongPathsEnabled`）；导入工具不会绕过它。

## 工具

`7z`、`ffprobe` 和 `ffmpeg` 按以下顺序查找：
1. `--tools` 文件夹（在窗口中：*Tools folder…*）；
2. `PATH`（在 Windows 上会自动加上 `.exe`）；
3. 常见的安装位置：`/opt/homebrew/bin`、`/usr/local/bin` 和 `C:\Program Files\7-Zip`。从 macOS Finder 启动的应用只会获得 `/usr/bin:/bin:/usr/sbin:/sbin` 作为 `PATH`，因此 Homebrew 的工具通过这些位置找到。

缺少 7-Zip 只影响 `.7z`/`.rar` 文件。缺少 `ffprobe` 会导致视频未经检查，并给出警告。缺少 `ffmpeg` 意味着 `--optimize` 会原样复制视频，并会记录这一情况。

这些工具并未随应用打包。FFmpeg（LGPL 或 GPL，取决于构建版本）和 7-Zip（LGPL，外加 unRAR 许可条款）都有各自的许可证；单独安装它们可以使这些条款不涉及本应用，并让用户自行更新。

两个反编译器（unrpyc、rpycdec）**随应用打包**（见"构建应用"）；只有从源码运行导入器时才需要按下表获取。

## 构建应用

为没有 Python 的用户，PyInstaller 可以把窗口打包成单个程序：

```bash
python -m pip install pyinstaller pywebview rpycdec
```

```bash
sh third_party/unrpyc/fetch.sh            # unrpyc 的固定版本源码，将被嵌入
python importer/build_app.py
```

`build_app.py` 会把两个反编译器各自构建成独立的单文件可执行程序并嵌入应用（ unrpyc 来自 fetch.sh 固定校验和的源码，rpycdec 来自 pip）：应用在运行时仍以独立进程、对游戏脚本的副本来运行它们，与从源码运行时完全相同。哪一个没能构建（例如 fetch.sh 失败），应用仍会产出，只是运行时报告该反编译器缺失。
```

- Windows 上：`dist\importer\RSL Importer.exe`，单个文件。
- macOS 上：`dist/importer/RSL Importer.app`（24 MB；使用 Python 3.12、PyInstaller 和 pywebview 6.2.1 构建，在 macOS 15.7 上启动）。
- 页面 `rsl_import_gui.html` 作为数据被打包；PyInstaller 的 pywebview 钩子会添加 pywebview 自身的脚本。
- unrpyc 和 rpycdec 作为独立可执行程序被打包进应用（见上）。
- PyInstaller 不支持交叉编译：请在 Windows 上构建 `.exe`，在 Mac 上构建 `.app`。
- 构建文件输出到 `build/test/pyinstaller/`（反编译器在 `build/decompilers/`）。这些文件夹都已被 git 忽略。
- 应用未签名。Windows SmartScreen 会显示"Windows 已保护你的电脑"：点击 *More info*（更多信息），然后点击 *Run anyway*（仍要运行）。在 macOS 上，首次运行时请右键点击应用并选择 *Open*（打开）。

## 导出内容

只复制游戏目录，并且在导出中始终命名为 `game/`。它会被放到 `switch/renpy-loader/games/<id>/game/`，旁边是 `manifest.json`，以及存在时的 `cover.jpg`（见"封面"）。

以下内容不会被复制：
- `renpy/`、`lib/` 以及 `.exe`/`.app`/`.sh` 启动器（Switch 运行时有自己的引擎）；
- `game/saves/`（开发者本地的存档）；
- 符号链接、`.DS_Store`、`._*` 和 `__MACOSX`。

`--replace` 的工作方式：
- 新的 `game/` 会暂存在导出位置旁边，然后通过重命名换入。
- 已有的 `game/saves/` 会被移入新的 `game/`。Switch 上的 Ren'Py 会将存档写入此处，也会写入 `saves/<id>`。
- 之前选择的封面（旧 manifest 中为 `"cover_custom": true`：在窗口中、使用 `--cover` 或在 Switch 上选择的）会被保留，除非本次导出也选择了封面。
- `switch/renpy-loader/saves/` 从不被修改。

## 封面

加载器为游戏显示的内容（docs/LOADER_UI.md "封面"）：

| 文件 | 尺寸 | 加载器显示位置 |
|---|---|---|
| `cover.jpg` | 300×450（2:3） | 库（两种布局）和详情页 |

- 它是基线 JPEG（Switch 的解码器不读取渐进式 JPEG），由 FFmpeg 生成，单帧，`yuvj420p`。任何 FFmpeg 能读取的图片都可作为源（PNG、JPEG、WebP、BMP）。源不是 2:3 时，原图以 `scale=…:force_original_aspect_ratio=decrease` 等比缩放完整放入中间，空余部分以同一张图的放大高斯模糊版填充（`gblur=sigma=30`，GOG/Netflix 风）：原图的任何部分都不会被裁掉，而加载器以裁切方式填满其 2:3 格子——这里没有东西可裁。
- 自动：游戏的主菜单美术图。即 `gui.rpy` 中 `gui.main_menu_background` 为直接指定图片名的普通字符串时使用的图片，否则使用 `gui/main_menu.png`，可以是松散文件，也可以位于标准 `.rpa` 中。未找到：没有封面，报告显示 `Pictures: none`（图片：无）；加载器显示占位图。缺少 FFmpeg：没有封面，并会给出警告。
- 所选：`--cover IMAGE`，窗口中的 *Change cover…*（更换封面）（在导入页面导出前，或在库中立即对游戏操作），或 Switch 上的 *Change picture*（更换图片）（从相册中选取截图，保存为 `cover.bmp`）。manifest 会标记 `"cover_custom": true`，`--replace` 会保留它。FFmpeg 无法读取的所选图片会被拒绝，并显示其错误。
- 报告中的 `Pictures:`（图片）行会列出文件名，所选的则标注 `(chosen)`（已选择）。

## 通过 FTP 上传到 Switch

窗口中的 *Switch over Wi-Fi*（通过 Wi-Fi 连接 Switch）目标和 `--out ftp://HOST:PORT` 需要 Switch 上运行 FTP 服务器（匿名访问；默认端口 5000，除非另行指定），其中 `/` 即为 SD 卡：
- 加载器自带的服务器：在库中按 +，然后打开 *Add games over Wi-Fi*（通过 Wi-Fi 添加游戏）。该页面打开期间服务器运行，且只能访问 `switch/renpy-loader/`；离开该页面时会重新读取库（`platform/transfer/ftpd.c`，[LOADER_UI.md](LOADER_UI.md)）；
- 或 sphaira 的服务器，它在加载器或游戏运行时会停止。

地址由路由器分配，因此可能会变化。仅使用 Python 的 `ftplib`。

上传的工作方式：
1. 如果 `games/<id>` 已存在且未启用 *Replace*（`--replace`），则不发送任何内容。
2. 每个文件都会上传到隐藏文件夹 `games/.uploading-<id>/`，`manifest.json` 最后上传。加载器会跳过以点开头的名称。早前上传遗留的 `.uploading-<id>` 会先被删除：它从不保存存档。
3. 点击 *Cancel*（取消）、连接中断或出现任何错误时，会通过新的连接删除 `.uploading-<id>`，Switch 保持原状。如果这也失败，下次上传时会将其删除。
4. 然后将其重命名到位：旧的 `games/<id>` 变为 `games/.old-<id>`，`.uploading-<id>` 变为 `games/<id>`，旧的 `game/saves/` 被移入新的 `game/`，之前选择的封面同样如此（见"封面"），最后删除 `.old-<id>`。
5. 如果第 4 步中的某个重命名失败，错误信息会同时列出两个文件夹。遗留的 `.old-<id>` 可能保存有存档，因此在手动移动或删除之前，该游戏的下一次上传会被拒绝。

`switch/renpy-loader/saves/` 从不被修改。与只替换 `game/` 和 `manifest.json` 的 SD 卡导出不同，上传会替换整个 `games/<id>/` 文件夹。

包含数千个小文件的上传速度约为 1–2 MB/s（逐个文件传输）。

## 删除游戏

在库中对游戏点击 *Delete…*（删除）会先询问，然后从 SD 卡或通过 FTP 删除 `games/<id>/`。除非勾选了 *Also delete its saves*（同时删除其存档），否则其存档会保留在 `switch/renpy-loader/saves/<id>/` 中。`games/<id>/game/saves/` 中的副本会随游戏一起删除：Ren'Py 会将每个存档写入其所有存档文件夹（`renpy/savelocation.py` 中的 `MultiLocation.save`），因此 `saves/<id>/` 中保存的是相同的存档。`switch/renpy-loader/logs/<id>/` 中的日志会被保留。没有对应的命令行选项。

## 与一组游戏文件夹比较

库中的 *Compare with folder…*（与文件夹比较）会选择 PC 上的一个游戏源文件夹：其中直接包含的游戏文件夹以及 `.zip`、`.7z` 或 `.rar` 文件（其他文件会被忽略）。它会将每个游戏与目标位置（SD 卡或 FTP）上的库进行比较，并且不会改动任何一方。

| 状态 | 含义 |
|---|---|
| *Different*（不同） | 库中的副本已与源文件不一致。详情会说明原因：文件数量、总大小，或哪些 `.rpa`/`.rpyc` 文件不同。 |
| *Not on the Switch*（Switch 上没有） | 库中没有游戏与之匹配。该行会显示导入时会生成的 ID。 |
| *Only on the Switch*（仅 Switch 上有） | 库中有一个游戏与文件夹中的任何源都不匹配。 |
| *Can't compare*（无法比较） | 匹配的游戏没有可用的 `manifest.json`（手动复制，或 manifest 被编辑过）。 |
| *Not a game*（不是游戏） | 导入工具无法将其识别为 Ren'Py 游戏；会显示原因。 |
| *Same*（相同） | 与 manifest 记录的源内容一致。 |

源的匹配与检查方式：
- 它会与 `manifest.json` 中记录的源名称（`source.name`，即文件夹或压缩包名称）相同的导出进行匹配。否则，会与导入时会生成的 ID 相同的导出进行匹配，该 ID 来自 `config.name` 或文件夹名称。通过 ID 匹配时，会说明库中的副本来自哪个源，例如旧版的 `Game-1.0.zip` 与 `Game-1.1.zip` 并存的情况。使用自定义 `--id` 导出、之后又被重命名的游戏，两种方式都无法匹配。
- 选择游戏文件夹及其文件的规则与导入相同。`--recompile` 导出在比较时不包含 `.rpyc` 文件；对于 `--decompile` 导出，会跳过总大小的比较，因为反编译器写入了部分导出文件。
- 先比较文件数量和总大小。如果两者都一致，则会对每个 `.rpa` 和 `.rpyc` 计算 SHA-256 哈希，并与 manifest 中的 `sha256` 比较。如果其他文件（图片、音频、`.rpy`）发生变化，但文件数量和总大小保持不变，则不会被发现。
- 压缩包会逐个解压到临时文件夹中，与导入时相同。包含大量大型压缩包的文件夹可能需要数分钟。*Cancel*（取消）会在当前游戏完成后停止。

## 游戏 ID 与标题

- **标题：** 取自 `game/options.rpy` 中的 `config.name`。如果缺失，则使用文件夹名（或压缩包名）。
- **ID：** 由标题生成；如果标题中没有 ASCII 字母或数字，则由文件夹名生成。
  - 转为小写；保留 `a-z` 和 `0-9`，其他连续字符合并为一个 `-`。
  - 去除首尾的 `-`，并限制最长为 **63** 个字符。加载器会跳过 64 字节或更长的文件夹名（`app/launcher/main.c`）。
  - 如果什么都不剩，则使用 `game`。
  - 示例：`The Question` → `the-question`；`ドキドキ` → `game`。

ID 即文件夹名，因此也是存档和日志的文件夹名。如果两个游戏最终得到相同的 ID，请为第二个游戏使用 `--id`。

## 检查项

| 检查项 | 方法 | 影响 |
|---|---|---|
| 游戏目录 | Ren'Py 自身的 `path_to_gamedir` 规则：启动器名称（`<Name>.exe/.sh/.py/.app`，也会按空格和 `_` 拆分），然后是 `game`，接着是 `data`，最后是文件夹本身（如果其中包含脚本）。可以穿过一层包装文件夹，或 `<Name>.app/Contents/Resources/autorun`。 | 找不到 `.rpy/.rpyc/.rpa` → 拒绝 |
| 脚本数量 | `game/` 下的 `.rpa`、`.rpy`、`.rpyc`、`.rpym` | Manifest 中的 `files` |
| Ren'Py 版本 | `renpy/vc_version.py`（`version = '8.5.3…'`，8.1+）。然后是已编译的 `vc_version.pyc`。然后是 `renpy/__init__.py` 中的 `version_tuple = (7, 4, 11, …)`；7.5/8.0 的文件两者都有，由 Python 提示决定取用哪一个。对于没有 `renpy/` 的文件夹，使用父级的 `renpy/`（SDK 项目），并给出警告。 | 决定状态 |
| Python 版本 | 先看 Ren'Py 版本（8 → 3，≤7 → 2）。否则看 `lib/py3-*` 与 `lib/py2-*`、`python2.7`、`pythonlib2.7`、`python27.dll`。否则看 `.rpyc` 文件：`RENPY RPC2` 文件头，然后是第 1 个槽位的 pickle 协议（≥3 → Python 3；2 或更早 → Python 2）。仅解压 2 个字节。 | 提示相互冲突 → 警告 |
| 自定义 Python | `game/python-packages/`，`game/` 中的 `*.py`（非 `*_ren.py`） | 警告：仅限纯 Python 3.12；Switch 上不可用 `ctypes`、`ssl`、`sqlite3`、`mmap`、`subprocess` 或 `multiprocessing` |
| 原生代码 | `game/` 中的 `*.pyd`、`*.dll`、`*.so`、`*.dylib` | 警告：无法在 Switch 上加载 |
| 压缩格式 | `.rpa` 文件头为 `RPA-3.0` 或 `RPA-2.0` | 其他文件头 → 警告：需要游戏自身加载器的自定义格式 |
| 媒体 | 按扩展名统计音频和视频文件。运行时的 FFmpeg 几乎可以解码所有格式（WebM/VP9、MP4/H.264、HEVC、AV1、AVI、WMV、MPEG 等）；只有 MIDI 会被拒绝，因为 Ren'Py 无法播放它。 | `.mid` 给出警告。文件 ≥ 2 GiB → 警告（FAT32 无法容纳 ≥ 4 GiB 的文件）。解码开销：见"Switch 性能" |
| 非 ASCII 名称 | 导出路径中包含任何非 ASCII 字符 | Switch 的 SD 卡将名称解析为 Shift-JIS，且无法列出其中任何一个（硬件测试 20）。因此导出会将这些文件打包进 `game/rsl-nonascii.rpa`（每 2 GiB 新建一个压缩包：`rsl-nonascii-2.rpa`，……），名称使用 NFC 形式，Ren'Py 从中读取。脚本（`.rpy`、`.rpym`、`_ren.py`）和 `.rpa` 文件保持松散存放，因为 Ren'Py 从不从压缩包中加载它们：给出警告 |

## 状态

| 检测到 | `status` | `runtime` |
|---|---|---|
| Ren'Py 8.x | `experimental` | `renpy-8.5.3` |
| Ren'Py 8.0–8.4 | `experimental`，并附带警告（8.5.3 加载旧版 `.rpyc` 未经测试） | `renpy-8.5.3` |
| 版本未知，Python 3 `.rpyc` | `experimental`，并附带警告 | `renpy-8.5.3` |
| Ren'Py ≤ 7.x，或 Python 2 | `unsupported`（尚无 Python 2 运行时） | `null` |
| Ren'Py ≤ 7.x 且使用 `--recompile` | `experimental`，并附带警告 | `renpy-8.5.3` |
| Ren'Py 9+，或无法识别 | `unsupported` | `null` |

导入工具从不写入 `verified`。只有硬件测试才能做到这一点（`docs/HARDWARE_TESTS.md`）。
它不会导出 `unsupported` 游戏，因为没有任何运行时能启动它们。

## 清单（`manifest.json`，版本 1）

```json
{
  "format": "renpy-switch-loader/manifest", "version": 1,
  "id": "the-question", "title": "The Question",
  "source": {"type": "directory", "name": "the_question"},
  "renpy_version": "8.5.3.26051504", "python": "3",
  "runtime": "renpy-8.5.3", "status": "experimental",
  "reasons": [], "warnings": ["..."],
  "files": {"count": 172, "total_bytes": 10637851, "rpa": 0, "rpy": 52, "rpyc": 52, "rpym": 1},
  "media": {".opus": 1},
  "recompile": false, "decompiled": [],
  "suggested_options": {"optimize": "1 videos above 30 fps (gui/splash.webm): re-encoding them …"},
  "performance": {"issues": ["..."], "converted": {"gui/splash.webm": "vp9"},
                  "coverage": {"loose_media": 3190, "archived_media": 0, "locked_archives": []}},
  "sha256": {"script.rpyc": "…"},
  "cover": "cover.jpg"
}
```

- `reasons` 说明游戏为何是 `unsupported`。
- `sha256` 列出每个 `.rpa` 和 `.rpyc`，路径相对于 `game/`。
- 对于 `--recompile` 导出，`recompile` 为 `true`。
- `suggested_options` 列出可以解决已发现问题的导入选项，并说明原因。`optimize`：可以被 `--optimize` 重新编码的、高于 30 fps 的视频（非 alpha，且有对应编码器的编解码器）。`recompile`：Python 2 游戏，其每个 `.rpy` 源文件都存在，且没有 `.rpa`（这是 `--recompile` 所需的条件）。`decompile`：没有 `.rpa` 的 Python 2 游戏，其已编译脚本没有 `.rpy` 源文件。选项开启后此项会清空。报告会将其打印为 `Suggested: --optimize: …`（建议：--optimize：……），窗口则在该选项下方以提示形式显示。
- `decompiled` 列出 `--decompile` 重建的 `.rpy` 文件（路径相对于 `game/`）。
- `cover` 指定图片，路径相对于游戏文件夹；没有图片时此项不存在。`cover_custom` 对于用户所选的图片为 `true`（见"封面"）。加载器的 *Change picture*（更换图片）会在那里写入一个 `.bmp` 文件，并使用 cJSON 重写 manifest（使用制表符，键被移到末尾）；其余内容保持不变。
- 加载器会读取 `status`、`runtime`、`renpy_version`、`recompile`、`reasons[0]`、`title`、`cover` 和 `performance.issues`：
  - 其游戏库会显示每个游戏的 Ren'Py 版本、状态、运行时，以及该运行时是否已安装；
  - 启动会运行 `runtimes/<runtime>.nro`。`renpy-8.5.3` 会回退到 `runtimes/renpy-runtime.nro`；
  - 它会拒绝 `unsupported` 游戏，以及缺少运行时的游戏，并说明原因；
  - 没有 manifest 的游戏（手动复制）使用默认运行时。

## Switch 性能

Switch 使用约 1 GHz 的 Cortex-A57 以软件方式解码视频。其屏幕在掌机模式下为 1280×720，底座模式下为 1920×1080，GPU 的最大纹理为 4096×4096。导入工具会标记那些会拖慢主机加载的内容。检查结果会写入 manifest 的 `performance.issues`，加载器会用 `!` 标记这类游戏，并显示原因。

检查项（视频需要 `ffprobe`；图片从文件头读取）。检查范围包括松散文件**以及**标准 `.rpa` 压缩包的内容：
- RPA-3.0 和 RPA-2.0，即 Ren'Py 自身的格式，按 `renpy/loader.py` 的读取方式读取；
- 索引（一个 pickle）使用 `pickletools` 逐个操作码解释。只接受普通数据（字典、列表、元组、字符串、字节、整数），包含导入或调用任何内容的索引会被拒绝。从不反序列化或执行任何内容。

其他格式的压缩包被视为**加密（locked）**。它们的内容不被检查，导入工具也不尝试打开它们：不解密，不破解格式。报告会列出它们，加载器会将其显示为一个问题。报告中的 `Checked:`（检查）行（以及 manifest 中的 `performance.coverage`）给出：
- 已检查的松散和压缩包内图片/视频数量；
- 加密的压缩包。

| 发现项 | 重要原因 | 是否由 `--optimize` 修复 |
|---|---|---|
| 帧率高于 30 fps 的视频 | 每多一帧都会消耗 CPU 和产生热量；屏幕上几乎看不出差别 | 是：以 30 fps 重新编码，尺寸不变。从压缩包中提取时，结果会作为松散文件导出；Ren'Py 会先加载磁盘上的文件，再加载压缩包，而压缩包则原样复制 |
| 高于 1920×1080 的视频 | 解码过于繁重，无法流畅播放 | 否（见下文） |
| 高于 1280×720 的视频 | 解码工作量约为 720p 的两倍 | 否（见下文） |
| HEVC 或 AV1 视频 | 软件解码中开销最大的编解码器 | 否 |
| 单边超过 4096 像素的图片 | 超过 GPU 纹理限制；可能无法显示 | 否 |
| 加密的压缩包 | 无法检查其资源 | 否 |

`--optimize` 的重新编码方式（在桌面端使用 FFmpeg；Switch 只播放结果）：
- 保持编解码器系列不变，因为游戏是按文件名引用文件的。VP9 → libvpx-vp9，CRF 31，启用分块（`-tile-columns 2`），以便 Switch 能在多个核心上解码。VP8 → libvpx。H.264 → libx264，CRF 20，profile high。
- 使用恒定质量（CRF）而不是固定码率，以保持画面清晰。音频原样复制。
- 跳过带有 alpha 通道的视频，以及没有对应编码器的编解码器，并会报告它们。
- 只修改导出内容。源文件从不被触碰。

为什么不自动缩小尺寸：Ren'Py 在不指定 `size=` 时，会以文件本身的像素尺寸绘制 `Movie` 以及任何图片。把 1080p 的开屏动画缩小到 720p，会使其只占屏幕的三分之二。缩小需要逐个游戏检查，因此导入工具只报告，不自动处理。

## 重新编译模式（`--recompile`）

适用于附带 `.rpy` 源文件的 Ren'Py 7.x 游戏（Python 2）。导出会去掉每个 `.rpyc`/`.rpymc`（Ren'Py 8 无法加载的 Python 2 pickle）以及 `game/cache/`。Ren'Py 8.5.3 随后会在首次启动时用 Python 3 编译这些源文件，首次启动会比较慢。源文件夹不会被修改。

在以下情况下，导入工具会拒绝重新编译模式：
- 游戏不是 Python 2；
- 它的某个 `.rpa` 压缩包**含有**已编译的脚本（`.rpyc`/`.rpymc`；导入器读取归档索引确认——索引只作为纯数据逐 opcode 解释，绝不反序列化），或索引无法读取（自定义格式、非纯数据），因此无法确认其中是否藏着脚本。归档里只装媒体时可以重新编译。拒绝时会指名该归档与其中的脚本；
- `tl/` 之外的某个 `.rpyc` 没有 `.rpy` 源文件，且未指定 `--decompile`。拒绝时会提示需要添加该选项。

在不使用 `--decompile` 的情况下，`tl/` 下没有源文件的 `.rpyc` 会被去掉，并给出警告：这些翻译可能会缺失。

### 反编译（`--decompile`）

适用于只附带 `.rpyc` 的 Ren'Py 7 游戏（部分或全部脚本没有 `.rpy`）。
`--decompile` 会为每个没有源文件的 `.rpyc`/`.rpymc` 重建一个 `.rpy`，包括 `tl/` 下的文件，然后像 `--recompile` 那样导出（它隐含 `--recompile`）。当 Python 2 游戏的 `.rpa` 压缩包都不含编译脚本、且其中存在从未反编译过的已编译脚本时，分析会建议使用它（`suggested_options`）。有两种反编译器可用；`--decompiler` 选择其中一种，窗口在 *Decompile*（反编译）下提供这两种：

| | unrpyc（默认） | rpycdec |
|---|---|---|
| 项目 | [CensoredUsername/unrpyc](https://github.com/CensoredUsername/unrpyc) v2.0.4，MIT。历史悠久的 Ren'Py 反编译器（自 2012 年起）。 | [cnfatal/rpycdec](https://github.com/cnfatal/rpycdec)，MIT。较新，规模较小。 |
| 安装 | 打包程序已内置。从源码运行时：不在 pip 上，`third_party/unrpyc/fetch.sh`（固定版本，带校验和）会将其放到 `build/unrpyc/unrpyc/`。 | 打包程序已内置。从源码运行时：`pip install rpycdec`。 |
| Python | 打包程序中无需 Python；从源码运行时用导入工具自身使用的 Python，3.9+（macOS 上的 `/usr/bin/python3` 可用）。 | 打包程序中无需 Python；从源码运行时需要 3.10+，因此不能用 `/usr/bin/python3`；通过 `--rpycdec-python PYTHON`（仅命令行）指定，或将 `rpycdec` 放在 `PATH` 上。 |
| Ren'Py | 面向 Ren'Py 8 编写；可读取 7，以及 6.18 起的 6。更旧的游戏会被拒绝。 | Ren'Py 7 和 8，已在 7.6.3、8.2.3、8.4.1 和 8.5.3 上测试，方法是将其输出重新编译并比较。 |
| 无法读取的脚本 | 会指出该脚本，并反编译其余部分。 | 在此停止：其余部分不被反编译，导入被拒绝。 |
| 无法读取的语句 | 保留为 `pass # <<<COULD NOT DECOMPILE: …>>>`。 | 保留为 `# <unrecognized: …>`。 |
| 副作用 | 其文件夹之外无任何影响。 | 从源码运行时，其包会向安装它的 Python 中添加替身 `renpy` 和 `store` 模块；打包程序不受影响。 |
| 打包应用 | 可运行（已内置）。 | 可运行（已内置）。 |

因此：优先使用 unrpyc。如果它无法读取某个脚本，请尝试 rpycdec，反之亦然。

```bash
sh third_party/unrpyc/fetch.sh            # 只需一次，用于 unrpyc
python3 importer/rsl_import.py OldGame --decompile --out /Volumes/SD
python3 importer/rsl_import.py OldGame --decompile --decompiler rpycdec --rpycdec-python ~/py312/bin/python
```

在没有 `sh` 的 Windows 上，请将 unrpyc 发布页面上的 v2.0.4 源代码压缩包解压到 `build\unrpyc\unrpyc\`，使 `build\unrpyc\unrpyc\unrpyc.py` 存在。

它与导入工具的隔离方式：
- 两者都使用受限的 unpickler 反序列化 `.rpyc` 文件，而 rpycdec 的 README 警告说没有任何防护是完美的。因此两者都不在导入工具内部运行。导入工具只会将没有源文件的 `.rpyc` 复制到其临时文件夹中，并以独立进程运行反编译器（标准输入关闭，`DECOMPILE_TIMEOUT` = 600 秒）。工作目录中只包含这些副本。
- unrpyc 使用 `--no-init-offset` 运行：它对 `init offset` 语句的推测只对 Ren'Py 8 安全，而且如果不使用此选项，输出的游戏内容相同，只是更冗长。
- 源游戏从不被触碰。重建出的 `.rpy` 会被导出，而每个 `.rpyc` 仍然会被去掉。
- 有 `.rpy` 源文件的 `.rpyc` 不会被反编译。
- 反编译器无法读取的脚本会被指出。`tl/` 之外的，导入会被拒绝（游戏需要它）；`tl/` 之下的则会被去掉，并给出警告（该翻译可能会缺失）。如果反编译器未能按时完成，导入也会被拒绝。
- 只反编译脚本。压缩包不被处理：即使是反编译模式，仍然会拒绝任何带有 `.rpa` 压缩包的游戏。不进行解密，也不使用这两个项目的其他工具（rpycdec 的存档重新签名，unrpyc 的 `--try-harder` 反混淆）。

反编译后的脚本可能与原件不同。注释和格式会丢失，语句可能被误读。manifest 会在 `decompiled` 下列出重建的文件，并且一条警告会说明同样的内容，并指出所用的反编译器。第二条警告会指出包含无法读取语句的文件；这些语句会被保留为注释。请完整游玩游戏进行检查。

这是一项实验，并非正式支持：
- Python 2 代码在 Python 3 下可能会有不同的行为，**且不会报任何错误**。例如，Ren'Py 7 脚本使用 Python 2 的除法，除非它们声明了 `rpy python 3`，因此 `7 / 2` 在那里是 `3`，而在这里是 `3.5`。请检查游戏的逻辑。
- 原游戏的存档无法加载。
- 对 Ren'Py 7 的正式支持，需要一个独立的 Python 2.7 运行时（`docs/COMPATIBILITY.md`，第 3 阶段）。

## 压缩包安全

每个压缩包在写入任何内容之前都会被检查。然后它会被解压到一个全新的临时文件夹中，之后该文件夹会被删除。导入工具会拒绝具有以下情况的压缩包：

- 绝对路径、盘符，或 `..` 组件；
- 在 Windows 上，Windows 无法按原样创建的名称（见"Windows 设置"）；
- 密码保护；
- 指向压缩包外部的符号链接。压缩包内的 zip 符号链接会被跳过，并给出警告。任何带有链接的 `.7z`/`.rar` 文件都会被拒绝，因为 7z 在解压之前无法提供链接目标；
- 超过 `MAX_FILES`（200,000）个条目；
- 解压后超过 `MAX_TOTAL_BYTES`（32 GiB）；
- 超过 1 MiB 的条目，其压缩比优于 `MAX_RATIO`（200:1）。对于 solid 的 7z/RAR，整个压缩包会根据其文件大小进行检查。

解压后，会再次遍历该目录树。届时发现的任何符号链接、硬链接或超出限制的情况都会导致导入被拒绝。

这些限制是 `importer/rsl/archives.py` 顶部的常量。

## 限制

- 不支持 `.tar.bz2`（Ren'Py 的 Linux 格式）或 `.dmg`。请先解压这些文件，然后导入文件夹。
- `.7z`/`.rar` 依赖于已安装的 `7z`。如果 7z 无法打开 RAR，导入工具会说明；请用其他工具解压，然后导入文件夹。
- 7z 的大小限制依赖于 7z 的列表信息。伪造的文件头只有在解压之后才会被发现。
- `.rpa` 内的脚本不会被检查；只检查图片和视频。如果游戏只附带 `.rpa` 而没有 `renpy/` 文件夹，其 Python 版本可能无法识别，从而被标记为 `unsupported`。
- `--recompile` 需要 `.rpy` 源文件。对于只附带松散 `.rpyc` 的 Ren'Py 7 游戏，请添加 `--decompile`（需要 unrpyc 或 rpycdec）。`.rpa` 压缩包内的脚本完全无法重新编译。
- 不检查 `game/` 中的 Python 模块是否导入了 Switch 缺少的模块。
- 媒体检查只看文件扩展名。运行时 FFmpeg 的编解码器列表尚未在硬件上确认。
- Windows 支持是在 macOS 上模拟 Windows 进行测试的（`WINDOWS`、`ntpath`、`cp1252` 控制台、模拟的工具调用），尚未在 Windows 机器上测试。
- 窗口仅在 macOS 上运行过。在 Windows 上，其 WebView2 后端（通过 pythonnet）和单文件 `.exe` 未经测试。
- 运行测试：`/usr/bin/python3 -m unittest tests/unit/test_importer.py tests/unit/test_importer_gui.py`。窗口测试在没有窗口的情况下驱动 `Api`，因此不需要 pywebview。在 Windows 上：
  `py -m unittest tests\unit\test_importer.py tests\unit\test_importer_gui.py`。
