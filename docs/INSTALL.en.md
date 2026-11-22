# Installation

[简体中文](INSTALL.md) · **English** · [Home](../README.en.md)

You need a Switch capable of running Atmosphère and Homebrew Menu. Copy `renpy-loader.nro` to `sdmc:/switch/renpy-loader/` and `renpy-runtime.nro` to `sdmc:/switch/renpy-loader/runtimes/`. Obtain available builds from [Releases](https://github.com/P4rfa1t/rpynx/releases), or build them as described in the [README](../README.en.md).

Hold **R** while starting an installed game from the HOME Menu, and continue holding it until Homebrew Menu appears. Open rpynx there. Games require full application memory; the loader refuses to start them in the limited-memory Album applet mode. The diagnostics page shows the current mode and offers a Python self-test.

The loader uses the following SD-card layout:

```text
switch/renpy-loader/
├── renpy-loader.nro
├── runtimes/renpy-runtime.nro
├── games/<id>/game/
├── saves/<id>/
├── config/controller/
├── cache/
└── logs/
```

Use the [desktop importer](IMPORTER.en.md) to analyse and export a legally obtained DRM-free game. Choose its options, press **Analyse**, review the report, select the target, and press **Export**. The target can be an SD card folder, the loader's Wi-Fi FTP page, or the experimental USB MTP page. Keep the corresponding Switch transfer page open while transferring. USB MTP still requires hardware verification; see [build status](../BUILD_STATUS.en.md).

Select a game in the library and launch it. The **+** menu provides controller settings and transfer pages. Games have their own menus and language preferences. Hold **+** and **−** together for three seconds to restore default controls. See [loader UI](LOADER_UI.en.md) and [compatibility](COMPATIBILITY.en.md) for details.
