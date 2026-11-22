# rpynx

[简体中文](README.md) · **English**

Play DRM-free Ren’Py games on a homebrew-enabled Nintendo Switch. rpynx combines a graphical game library, remappable controller inputs, and a desktop importer for preparing and transferring games. This is an unofficial project unaffiliated with Nintendo or Ren’Py.

**Preview.** Ren’Py 8.5.3 has run on Switch hardware. Hardware records cover the game library, saving and loading, and Wi-Fi FTP. USB MTP is implemented in the source and has passed host protocol tests. Actual Switch USB transfer remains unverified. See the [build and verification status](BUILD_STATUS.en.md).

## Install and play

1. Check [GitHub Releases](https://github.com/P4rfa1t/rpynx/releases) for available builds, or build from source below. Put `renpy-loader.nro` in `switch/renpy-loader/` on the SD card and `renpy-runtime.nro` in its `runtimes/` subdirectory.
2. Hold **R** while launching an installed game from the HOME Menu until Homebrew Menu appears, then open rpynx. Ren’Py games require full memory. The loader blocks game launch in Album applet mode.
3. In the desktop importer, select a game folder, ZIP, 7z, or RAR. Choose unpacking or decompilation options as needed, click **Analyse**, review the report, then choose a target and **Export**. 7z/RAR inputs require 7-Zip. Cover creation and video processing require FFmpeg.
4. Select and launch the game from the Switch library. The loader’s **+** menu opens controller settings. Use the game’s menu to save and load.

Export to an SD card or use the loader’s Wi-Fi FTP page. To test USB MTP, open **Add games over USB** from the loader’s **+** menu, connect a data cable, then scan for the device from the importer’s **Target** page. The packaged Windows importer uses WPD. macOS and Linux require `libmtp`. The desktop and Switch interfaces follow the system preference for Simplified Chinese, Traditional Chinese, or English and fall back to English for other languages.

Ren’Py 7 and earlier Python 2 games require migration. Results depend on their scripts and dependencies. Desktop precompilation is opt-in because it runs game initialization code. Use it only for trusted games. See the [installation guide](docs/INSTALL.en.md), [importer manual](docs/IMPORTER.en.md), and [compatibility notes](docs/COMPATIBILITY.en.md).

## Build from source

Run these commands from the repository root. Switch builds require a running Docker daemon on Linux, macOS, or Windows with WSL 2.

```bash
scripts/build.sh
```

```bash
scripts/build.sh python
scripts/build.sh renpy
scripts/build.sh runtime
```

Both NROs are written to `dist/`. See the [importer manual](docs/IMPORTER.en.md) for desktop packaging and [AGENTS.md](AGENTS.en.md) for testing commands.

## Documentation

| Topic | Guides |
| --- | --- |
| Status and verification | [Build status](BUILD_STATUS.en.md), [hardware tests](docs/HARDWARE_TESTS.en.md) |
| Design and interface | [Architecture](docs/ARCHITECTURE.en.md), [loader UI](docs/LOADER_UI.en.md) |
| Runtime builds | [CPython](runtime/python/README.en.md), [Ren’Py](runtime/renpy/README.en.md) |
| Contributor guidance | [AGENTS.md](AGENTS.en.md), [CLAUDE.md](CLAUDE.en.md) |

## License

The project source is [MIT-licensed](LICENSE) and derives from [nixentric/rpynx](https://github.com/nixentric/rpynx). Third-party components retain their own licenses. The runtime NRO links GPL-enabled FFmpeg. See the [release workflow](.github/workflows/release.yml) and individual release notes for its distribution terms and corresponding source. Users provide their own games.
