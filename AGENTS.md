# rpynx contributor guide

rpynx combines a Nintendo Switch loader, an embedded Ren’Py runtime, and a desktop game importer. Run commands from the repository root.

## Code map

- `app/launcher/` manages the game library, launching, and transfers. `app/ui/` implements the LVGL interface.
- `runtime/` embeds CPython and Ren’Py. The runtime bridge and compatibility hooks live in `runtime/renpy/boot/`.
- `platform/` provides filesystem, input, and transfer services. `platform/transfer/mtp_core.c` implements the MTP protocol, and `mtp_usb.c` connects it to USB on Switch.
- `importer/rsl_import.py` and `importer/rsl_import_gui.py` are the desktop entry points. `importer/rsl/` handles analysis, archives, migration, exporting, and the game library. `importer/mtp_windows/` contains the Windows WPD adapter.
- `.github/workflows/release.yml` defines release builds and artifact reuse. See [architecture](docs/ARCHITECTURE.md), the [importer manual](docs/IMPORTER.md), and the [hardware checklist](docs/HARDWARE_TESTS.md).

## Changes

- Base compatibility fixes on Ren’Py versions, script formats, asset layout, or identifiable legacy API behavior. Games can reproduce and verify a problem. The fix should apply to other games with the same behavior.
- Keep source games read-only during import. Transform and precompile staged copies. In the GUI, start analysis only after the user clicks **Analyse**, and invalidate old reports when inputs change. Keep desktop precompilation opt-in because it runs game initialization code.
- Treat archives and compiled scripts as untrusted. Reuse entry-name checks, size limits, and data-only parsing in analysis. Avoid unpickling game data in the importer process.
- Preserve shared export behavior across SD, FTP, and MTP, including cancellation, save and cover preservation, and recovery during replacement. Keep MTP scoped to the `switch/renpy-loader` library.
- Check Simplified Chinese, Traditional Chinese, and English when changing interface strings. The catalogs live in `importer/rsl/locale_*.py`, `app/ui/locale_*.inc`, and `runtime/renpy/boot/rsl_locale.py`. Preserve format placeholders, paths, configuration keys, and manifest fields. Games control their own language.

## Build and verify

| Task | Command |
| --- | --- |
| Loader NRO | `scripts/build.sh` |
| Ren’Py runtime NRO | Run `scripts/build.sh python`, `scripts/build.sh renpy`, then `scripts/build.sh runtime`. |
| Importer and MTP tests | `RSL_LANGUAGE=zh-CN python -m unittest tests.unit.test_importer tests.unit.test_importer_gui tests.unit.test_mtp` |
| Runtime compatibility tests | `python -m unittest tests.unit.test_compat_fixes` |
| Host C, FTP, MTP, and UI | `scripts/test.sh`. UI mockups require macOS `sips`. |
| Desktop Ren’Py integration | `tests/integration/renpy_desktop/run.sh` |
| Build the desktop package | Install the requirements in `importer/requirements.txt` and the build dependencies, then run `python importer/build_app.py`. Building the native Windows MTP component requires CMake and the MSVC x64 toolchain. |

Switch builds use Docker and produce NROs in `dist/`. Python tests run directly on Windows or WSL. POSIX FTP and MTP tests require host fixtures in `build/host/`. Report host tests, Switch cross-builds, and hardware tests separately. Confirm USB enumeration, disconnect recovery, game launch, and first-launch timing on actual hardware. The release workflow supports manual build verification. Release tags publish the artifacts.
