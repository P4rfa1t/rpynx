# Desktop importer

The importer prepares DRM-free Ren’Py games for rpynx. It analyses a directory, ZIP, 7z or RAR, reports potential compatibility and performance problems, and exports the game and its manifest to an SD card, FTP server or experimental USB MTP device. These are experimental compatibility tools; a successful export does not guarantee a game will run.

## Desktop window

1. Select a game folder or archive. Choose options before clicking **Analyse**. Changing the source or options invalidates the previous report; analyse again before exporting.
2. Inspect the report, warnings and suggested options. Select an SD card folder or connect to the Switch from the **Target** page.
3. Click **Export**. Replacement keeps existing saves and selected covers. Keep the transfer page open until a network or USB transfer finishes.

The interface follows the system language for Simplified Chinese, Traditional Chinese and English. Other languages fall back to English. `RSL_LANGUAGE` can override the desktop selection. Existing manifests may contain diagnostics saved in their original language.

## Command line

From a source checkout, install the dependencies in `importer/requirements.txt` and run:

```bash
python importer/rsl_import.py --help
```

```bash
python importer/rsl_import.py GAME.zip --out /path/to/sd-card
```

`--out ftp://HOST:PORT` uses FTP. `--mtp-list` lists detected rpynx USB targets; pass the returned `mtp://…` target to `--out`. `--id ID` chooses the game ID and `--replace` updates an existing export. `--cover IMAGE` chooses a cover; `--tools DIR` specifies a tool directory. ZIP and folders use Python's standard library; 7z/RAR require 7-Zip. FFmpeg/ffprobe enable artwork generation, video inspection and optimization.

## Python 2 and performance options

- `--recompile` stages Python 2 source scripts and migrates their Python blocks for Ren’Py 8.5.3. Existing compiled scripts are not carried over. Conversion is experimental.
- `--decompile` rebuilds missing source from compiled scripts using `unrpyc` or `rpycdec`, then enables recompilation. Decompilers run as separate processes. `--decompiler` chooses the tool and `--rpycdec-python` can select an interpreter for rpycdec.
- `--unrpa` extracts standard `.rpa` members into a staged game directory, making archived scripts available for decompilation. Archive entry names and size limits are checked.
- `--precompile` is an explicit Windows option. It downloads the official Ren’Py 8.5.3 SDK if needed, checks its SHA-256, and runs compilation on a staged copy. **This executes game initialization code with your user permissions; use only trusted games.** Staging is not a sandbox.
- `--optimize` re-encodes supported videos above 30 fps at 30 fps without resizing. Check the report before choosing it.

The source game is not modified by the importer's staging operations. Games can still depend on unsupported Python modules, native code, custom archives or behavior that automatic migration cannot handle.

## Export and transfer

An exported game normally lives at `switch/renpy-loader/games/<id>/game/`, with `manifest.json` beside it. The importer also preserves referenced items next to `game/` when it detects literal ASCII `../<name>` references in loose scripts or standard archives. Dynamically constructed paths and custom archive formats may not be detected. The source folder is scanned without unpickling game scripts in the importer process.

For FTP, open **Add games over Wi-Fi** in the Switch loader's **+** menu, then enter the displayed address. For USB, connect a data cable, open **Add games over USB**, and select the detected rpynx device in the importer. The Windows package uses WPD; macOS and Linux require `libmtp` (for example, `brew install libmtp`). MTP exposes only the rpynx library and limits individual objects to approximately 4 GiB. USB transport is implemented and host-tested but has not yet been validated end to end on Switch hardware.

Remote replacement uses staging and old-copy directories. If a disconnect interrupts the final replacement, inspect `.old-<id>` for saves before removing it. Separate per-game saves under `switch/renpy-loader/saves/` are not removed unless explicitly selected for deletion. The GUI also supports library browsing, cover selection and read-only comparison against a local collection.

## Build and tests

Install `pywebview`, PyInstaller and `importer/requirements.txt`, then run `python importer/build_app.py` on the target desktop platform. Building the Windows WPD component requires CMake and the MSVC x64 toolchain. See the [contributor guide](../AGENTS.md) for test commands and the [build status](../BUILD_STATUS.md) for verification limits.

## Windows setup and tool lookup

Install Python and pywebview if using the GUI from source. The Windows GUI uses Edge WebView2; install Microsoft's Evergreen runtime if Windows 10 cannot open the window. The importer checks the explicit tools directory, then `PATH`, then common installation paths such as `/opt/homebrew/bin`, `/usr/local/bin` and `C:\Program Files\7-Zip`. A bare drive such as `--out E:` means its root. Paths over 260 characters depend on Windows long-path support. Tool subprocesses run without console windows, and 7-Zip lists names in UTF-8.

The packaged importer includes available decompilers as executables and runs them on staged copies in separate processes. Obtain pinned unrpyc sources with `sh third_party/unrpyc/fetch.sh`; rpycdec is available via pip and requires Python 3.10+. FFmpeg and 7-Zip are separate installations. Without ffprobe, video inspection is skipped with a warning; without ffmpeg, optimization falls back to copying. PyInstaller builds on the target OS. Its outputs are `dist/importer/RSL Importer.exe` on Windows and `dist/importer/RSL Importer.app` on macOS. The packages are unsigned.

## Covers and library management

The loader displays a 300×450 baseline JPEG (`cover.jpg`). The importer looks for a literal `gui.main_menu_background` in `gui.rpy` or the default `gui/main_menu.png`, including eligible standard RPA entries. FFmpeg fits the original inside the frame and fills the margins with a blurred copy. Without artwork, the loader displays a placeholder. Choose an image with `--cover` or **Change cover**. The Switch can also save an Album screenshot as `cover.bmp`. User-selected covers are recorded with `cover_custom` and preserved during replacement unless a new cover is selected.

**Delete** removes the selected game after confirmation. Its separate `saves/<id>/` directory is kept unless explicitly selected for deletion. A save copy under `games/<id>/game/saves/` goes with the game; logs are retained. **Compare with folder** is read-only and matches direct child game folders and supported archives by recorded source name, then generated ID. Results distinguish matching, different, missing, library-only, unrecognizable and uncomparable entries. Files are compared by count and total size, followed by SHA-256 of relevant `.rpa` and `.rpyc` files. Equal-size edits to other files may go unnoticed. Comparison adapts to recompiled and decompiled exports; cancellation stops after the current game.

## Identification and checks

The title comes from `config.name` in `game/options.rpy` or the source name. IDs contain lowercase ASCII letters, digits and hyphens, are limited to 63 characters, and fall back to `game` when no usable text remains. Select `--id` for collisions. IDs also identify the game’s save and log directories.

The importer searches launcher-derived names, `game`, `data`, and a script-containing source root. It handles a wrapper folder and macOS app autorun layouts. It counts scripts and media, examines engine version metadata and Python directory hints, and checks compiled script protocol markers without deserializing the script. Conflicting hints produce warnings. Native libraries are flagged, as are custom Python packages that may require modules unavailable on Switch, including `ctypes`, `ssl`, `sqlite3` and `mmap`. Unfamiliar archive headers and MIDI are reported. FAT32 cannot hold individual files of 4 GiB or more.

| Input | Result |
| --- | --- |
| Ren’Py 8.x | Experimental with `renpy-8.5.3`; older compiled formats may generate warnings. |
| Unknown version with Python 3 compiled scripts | Experimental with warnings. |
| Ren’Py 7 or earlier / Python 2 | Unsupported until eligible scripts are migrated. |
| Migrated Python 2 game | Experimental with `renpy-8.5.3`. |
| Ren’Py 9+ or an unidentified engine | Unsupported. |

## Manifest version 1

`manifest.json` records `format`, `version`, `id`, `title`, `source`, `renpy_version`, `python`, `runtime`, `status`, `reasons`, `warnings`, `files` and `media`. `recompile` and `decompiled` describe script migration; `suggested_options` explains relevant optional operations. `performance` records `issues`, `converted` and `coverage`, including loose and archived media and locked archives. `sha256` maps archive and compiled-script paths to digests. `cover` and `cover_custom` describe artwork. The loader selects `runtimes/<runtime>.nro`, falling back to `renpy-runtime.nro` for `renpy-8.5.3`. It refuses unsupported games or missing runtimes. The importer never assigns `verified`; see the hardware checklist.

## Performance checks

Image dimensions come from headers; video checks require ffprobe. The report includes loose and standard archived assets. It flags textures above 4096 pixels on either side, videos above 720p or 1080p, HEVC/AV1 software decoding costs, frame rates above 30 fps and archives it cannot inspect. Switch targets are 1280×720 handheld and 1920×1080 docked. Eligible non-ASCII assets are packed into `rsl-nonascii.rpa` and additional numbered archives with NFC names. Scripts and archives remain loose and may produce warnings.

Optimization limits supported video to 30 fps without resizing or changing the codec family, copying audio. VP9 uses libvpx-vp9 at CRF 31 and two tile columns; VP8 uses libvpx; H.264 uses libx264 at CRF 20 with the High profile. Optimized archived video is exported as a loose override. High resolution and large textures still require manual review.

## Archive checks and migration limits

Archive extraction uses a temporary directory and validates entry names before writing. Absolute paths, parent components, password-protected inputs and Windows-unsafe names are refused. ZIP symlinks are skipped with a warning when safe; archives with unsafe links are refused, and 7z/RAR links are rejected. Checks limit extraction to 200,000 entries, 32 GiB uncompressed, and a 200:1 ratio for entries above 1 MiB. Solid archives are checked using their total sizes. The extracted tree is checked again for links and limits. `.tar.bz2` and `.dmg` must be unpacked separately. The standard RPA index reader accepts only data opcodes and does not execute the pickle. Custom archives may remain opaque.

Decompilation can lose comments and formatting, omit unreadable statements or change behavior. Review warnings and the `decompiled` list. Python 2 semantics can differ silently, for example integer division. Original Python 2 saves do not carry over automatically to the migrated runtime. For archive-only games, the importer may still lack enough information to identify an engine. Use `--unrpa` to expose eligible archived scripts before migration. Import checks do not exhaustively inspect Python imports or certify every hardware codec.
