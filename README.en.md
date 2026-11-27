# rpynx

[简体中文](README.md) · **English**

Play DRM-free Ren’Py games natively on a homebrew-enabled Nintendo Switch. rpynx provides an LVGL graphical library, remappable controller bindings, on-device ZIP extraction, automatic 2:3 cover extraction, Wi-Fi FTP / USB 3.0 MTP transfer, and a desktop importer. This is an unofficial project unaffiliated with Nintendo or Ren’Py.

## Status

Both native runtimes (**Ren’Py 8** and **Ren’Py 7**) are verified on Switch hardware:

| Runtime | Engine & Python | Target games | Hardware status |
| --- | --- | --- | --- |
| `renpy-runtime.nro` | Ren’Py 8.5.3 + CPython 3.12.8 | Ren’Py 8.x | Verified (`Z.A.T.O.`, `The Freak Circus`, `Prescription: LOVE`, etc.) |
| `renpy-7.8.7.nro` | Ren’Py 7.8.7 + CPython 2.7.18 | Ren’Py 6.99 / 7.x | Verified (`Doki Doki Literature Club!`, `Butterfly Soup`, `Our Life`, etc.) |

## Install and play

1. Download the NRO builds from [GitHub Releases](https://github.com/P4rfa1t/rpynx/releases) or build from source below, and place them on your Switch SD card:
   - `sdmc:/switch/renpy-loader/renpy-loader.nro`
   - `sdmc:/switch/renpy-loader/runtimes/renpy-runtime.nro`
   - `sdmc:/switch/renpy-loader/runtimes/renpy-7.8.7.nro`
2. Hold **R** while launching an installed game from the HOME Menu to enter Homebrew Menu with full memory access, then launch `renpy-loader`. Game launch is refused in Album applet mode.
3. Add DRM-free Ren’Py game folders or `.zip` archives via SD card, Wi-Fi FTP, USB MTP, or the desktop importer (`importer/`). Because Ren’Py 7.8.7 runs natively, Ren’Py 7 games no longer require decompilation or `.rpa` unpacking.
4. Select and launch any game from the library. Press **X** in the library or open controller settings in game details to customize bindings.

## Build from source

Run from the repository root with Docker running:

```bash
scripts/build.sh

scripts/build.sh python
scripts/build.sh renpy
scripts/build.sh runtime

scripts/build.sh python2
scripts/build.sh renpy7
scripts/build.sh runtime7
```

See [docs/IMPORTER.md](docs/IMPORTER.md) for desktop packaging and [AGENTS.md](AGENTS.md) for test commands.

## Documentation

| Topic | Guides |
| --- | --- |
| Status and verification | [BUILD_STATUS.md](BUILD_STATUS.md), [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) |
| Design and interface | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/LOADER_UI.md](docs/LOADER_UI.md) |
| Setup & importer | [docs/INSTALL.md](docs/INSTALL.md), [docs/IMPORTER.md](docs/IMPORTER.md), [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) |
| Runtime builds | [runtime/python/README.md](runtime/python/README.md), [runtime/renpy/README.md](runtime/renpy/README.md) |
| Contributor guidance | [AGENTS.md](AGENTS.md) |

## License

The project source is [MIT-licensed](LICENSE) and derives from [nixentric/rpynx](https://github.com/nixentric/rpynx). Third-party components retain their own licenses. The runtime NROs link GPL-enabled FFmpeg; see the [release workflow](.github/workflows/release.yml) and release notes for source availability. Users provide their own games.
