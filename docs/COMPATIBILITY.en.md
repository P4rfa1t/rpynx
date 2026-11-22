# Compatibility matrix

**English** · [简体中文](COMPATIBILITY.md)

Statuses: **Verified** = passed the defined tests on real hardware. **Experimental** = loads
but tests are incomplete or issues are known. **Unsupported** = cannot run reliably.
**Hardware unverified** = host-side tests only, or no run evidence yet; never counts as
hardware verification.

## Engine generations

| Ren'Py | Python | Status | Notes |
|---|---|---|---|
| 8.5.x | 3.12 | **Experimental** | Ren'Py 8.5.3 runs on the Switch (GLES 3.2 on nouveau); first game launched 2026-10-18 |
| 8.0–8.4 | 3.9 / 3.12 | **Experimental** | Untested on hardware. The importer accepts them with a warning; Phase 2: test whether 8.5.3 loads their `.rpyc` |
| 7.x | 2.7 | Unsupported | Phase 3: separate Python 2.7 runtime, if feasible. Experiment: the importer's `--recompile` exports a game with `.rpy` sources for Ren'Py 8.5.3 (experimental; Python 3 semantics). Hardware evidence: Forbidden Fantasy (7.5.3), recompiled on the console, runs (runs 13–14, 24–25, 28), but that is a per-game result, not general support |
| ≤ 6.x | 2.x | Unsupported | Not planned |

## Games

Every entry below has a test record in `docs/HARDWARE_TESTS.md`; games without one are not
added to this table.

| Game | Source | Ren'Py | Status | Tested on | Notes |
|---|---|---|---|---|---|
| The Question | bundled with the Ren'Py SDK | 8.5.3 | **Experimental** | Switch, handheld, application mode, 2026-10-18 (run 5): starts, renders on GPU, dialogue, menus, clean quit | Also launched from the importer's export (`games/the-question`, run 9). After the return in run 9 the loader crashed on SDL's timer thread; fixed in runs 10–12 and re-checked on hardware. Music plays without stutter (run 21). This game's saves, suspend and touch have not been checked on hardware on their own (the same behaviour was checked with other games and screens in runs 23–24, 26, 28–29) |
| Forbidden Fantasy | PC commercial game (Ren'Py 7.5.3, recompiled at import) | 7.5.3 | **Experimental** | Switch, handheld, application mode, 2026-10-19 (run 13): 160 `.rpy` recompiled on the console, GLES 2 rendering, episode menu and in-game menus | In run 13 the episode buttons' text was reset by Ren'Py 8.5.3's language switch (gui values), fixed before run 14; keyboard, menu focus, D-pad navigation of overlapping buttons, and link/folder notices were fixed in runs 14–19 and verified in runs 24–25. Run 28 launched it from the library and quit cleanly. Save/load not recorded on its own |
| JP Name Test | synthetic test game made for this project, not committed | 8.5.3 | **Experimental** | Switch, handheld, application mode, 2026-10-21 (runs 21–27): non-ASCII files inside a `.rpa`, audio, save/load, remapping, custom screen, HOME, sleep | Used only to exercise specific behaviour; it is not a reproducible release game (the source files are not committed). Non-ASCII loose files remain unreliable on the SD card; the same names work inside a `.rpa` (runs 20–21) |

## Loader features

"Hardware verified" in this table means a real-console record under `tests/hardware/results/`
only; host-side and desktop integration tests are listed separately and never count as
hardware verification.

| Feature | Status |
|---|---|
| Controller normalisation, mapping, remapping, persistence | Implemented, host-tested; on hardware: run 1 (menu navigation), 22 and 26 (remapping, a remapped button used in a game), 27 (in-game emergency reset saved to the global config). Verified in handheld mode; docked mode and controller reconnect still unverified |
| Virtual cursor, touch readout | Implemented, host-tested (cursor math); on hardware: runs 16–19 (cursor movement and hiding), 27 and 29 (finger drag of the picture frame). Touch coordinate readout (H7) and hiding the cursor after touch are not recorded on their own |
| Emergency recovery combo | Implemented, host-tested; on hardware: run 22 (loader), 23 (in game), 26–27 (in-game reset saved to the loader too). Verified |
| Suspend / resume / reconnect input reset | Implemented, host-tested (engine); sleep/resume log lines in runs 23–24, 26, the game continued. Controller reconnect not tested on hardware |
| Embedded Python 3.12.8 runtime + self-test | Builds and links; packaging host-tested; on hardware the self-test completed in runs 4, 20, 28 (66/66 required imports). Verified (non-ASCII file names on `sdmc:` are a known failure, see run 20) |
| Ren'Py 8.5.3 engine in the runtime | Builds and links; launched and rendered repeatedly on hardware from run 5 on. Verified |
| Controller bridge into Ren'Py | Desktop test 24/24–26/26 with the official engine; on hardware runs 16–19, 26 verify the D-pad, A/B and text input. Verified (docked mode and the cursor after touch excepted) |
| Game launch from the library, per-game saves and logs | Implemented; hardware runs 9, 13, 28 launched from the library, run 26 saved and loaded the same line. Verified |
| System language (Simplified Chinese / Traditional Chinese / English) | Implemented; the loader and runtime read the Switch system language, the desktop importer reads the system locale or `RSL_LANGUAGE`. The mapping and fallback are covered on the host by `test_system_locale_mapping_and_catalogs` in `tests/unit/test_mtp.py`; **unverified on the Switch** (see H57a–H57f) |
| Send games over Wi-Fi (FTP) | Implemented; host-side `tests/unit/test_ftpd.py` passes; hardware run 29 sent, replaced, deleted and interrupted a transfer, all PASS |
| Send games over USB (MTP) | Implemented (Switch side `platform/transfer/mtp_core.c`, `mtp_usb.c`; Windows side `importer/mtp_windows/`); host-side `tests/unit/test_mtp.py` covers export, replacement, cancel, the MTP wire protocol (POSIX harness) and the UI language mapping; **actual USB transfer on the Switch is unverified** (see H58–H62) |
| Live2D, 3D models (GLTFModel) | Unsupported (no Switch libraries) |
| In-game loader overlay | Not implemented (Milestone 6); the button shows a notice |
| Importer (`importer/rsl_import.py`, macOS/Windows/Linux) | Implemented, host-tested (25 tests); its export of The Question launched on hardware (run 9). See `docs/IMPORTER.md` |
