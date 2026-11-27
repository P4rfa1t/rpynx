# Build and verification status

This page distinguishes features implemented in source, host tests, and Switch hardware results. Historical runs apply to the builds they tested. The current revision still needs its own acceptance tests.

| Component | Evidence | Pending verification |
| --- | --- | --- |
| Loader | The LVGL library, cover art, layouts, details, and launch passed in [run 28](tests/hardware/results/2026-10-23-run28.md). | On-device glyph coverage and layout with the new language selection. |
| CPython / Ren’Py | [Run 4](tests/hardware/results/2026-10-18-run4.md) covers the CPython 3.12.8 self-test. [Run 5](tests/hardware/results/2026-10-18-run5.md) covers launching a sample game with Ren’Py 8.5.3. [Run 21](tests/hardware/results/2026-10-21-run21.md) covers audio. | Cross-build and launch of the new NROs. |
| Game input and reliability | [Run 26](tests/hardware/results/2026-10-21-run26.md) covers remapping and save/load. [Run 27](tests/hardware/results/2026-10-21-run27.md) covers blocking game launch in Album applet mode and exit behavior. | See the hardware checklist for coverage of docked mode, controller reconnect, and other scenarios. |
| Importer | On Windows, `ac04b4f` passed 70 regression tests and skipped two. Manual analysis, system-language selection, and the MTP target are implemented in source. | New packaged builds and UI checks on the supported desktop platforms. |
| Wi-Fi FTP | [Run 29](tests/hardware/results/2026-10-25-run29.md) verified CLI import, replacement, delete, and leaving the transfer page mid-transfer. | Hardware regression with the current desktop GUI. |
| USB MTP | Eight combined MTP/FTP host tests passed under Ubuntu. The Windows WPD DLL compiled and passed enumeration with no device attached. | Switch enumeration, actual uploads and downloads, disconnect/reconnect, save and cover preservation on replacement, and macOS/Linux device connections. |
| GitHub Actions | The release workflow uses Node.js 24 versions of checkout, setup-python, and the artifact actions. | Confirm artifacts and annotations in a new workflow run. |

## Known limits

- Non-ASCII filenames on the Switch SD card may be missing from directory listings. See [run 20](tests/hardware/results/2026-10-20-run20.md). The importer can pack eligible assets. Scripts and custom loaders need separate validation.
- Migration of older Ren’Py games remains experimental. The original DDLC archive verified the export layout and 35 decompiled scripts. The corrected export still needs a hardware launch test. The first-launch benefit of precompilation also needs hardware measurements.
- Individual MTP objects are limited to approximately 4 GiB. The Windows enumeration and host protocol tests ran without a Switch attached, and the current build still needs an end-to-end hardware transfer test.

Use the [hardware test checklist](docs/HARDWARE_TESTS.md) and [compatibility notes](docs/COMPATIBILITY.md) for further verification. Earlier milestone details remain in the [previous build status](https://github.com/P4rfa1t/rpynx/blob/ac04b4f/BUILD_STATUS.md).
