# Real Nintendo Switch test checklist

**English** · [简体中文](HARDWARE_TESTS.md)

Nothing in this project counts as hardware-verified until a row below has an actual
result. Emulator runs go in a separate column and never count as hardware verification.

Historical hardware test records are kept in English.

## Setup

1. Copy `dist/renpy-loader.nro` to `sdmc:/switch/renpy-loader/renpy-loader.nro`.
2. Optional: copy a profile from `config/profiles/` to
   `sdmc:/switch/renpy-loader/config/controller/global.json`.
3. Launch it twice: once from the album (applet mode), once by holding R while starting an
   installed game (title override, application mode). Note the mode shown on the main menu.

## Milestone 1–2 tests (current build)

| # | Test | Steps | Expected |
|---|---|---|---|
| H1 | Cold launch | Start from hbmenu | Main menu shows version, Handheld/Docked, applet type. `loader.log` gets "rpynx 0.1.0 …" |
| H2 | Folders created | After H1, browse SD | `logs/`, `config/controller/games/`, `games/` exist under `sdmc:/switch/renpy-loader/` |
| H3 | A/B orientation | Controller Test, press A, then B | A shows `confirm`, B shows `back`. Physical A is the right face button |
| H4 | Every button | Controller Test, press each button incl. ZL/ZR, +/−, stick clicks, D-pad | Each shows its own name under Physical, nothing else lights up |
| H5 | Sticks and deadzone | Controller Test, move both sticks slowly | Raw values change; normalised stays 0.00 inside the deadzone; full tilt is about 1.00 |
| H6 | Virtual cursor | Controller Test, right stick | Cursor moves, speeds up while held, stops at screen edges |
| H7 | Touch | Controller Test, handheld, touch the screen | Touch shows coordinates; cursor jumps to the touch point |
| H8 | Repeat | Main menu, hold D-pad down | One move, ~400 ms pause, then steady repeat |
| H9 | Remap and save | Controller Settings, remap `confirm` to X, `hide_ui` to A, press + | "Saved and applied"; X now selects in menus |
| H10 | Conflict | Remap `confirm` to B | Both rows marked conflict; + refuses to save with a message |
| H11 | Persistence | After H9, exit and relaunch | Mapping from H9 still active; Diagnostics says "loaded" |
| H12 | Emergency combo | Map `confirm` to SL (unusable), save, then hold + and − for 3 s | Default controls restored and settings opened. "Emergency reset" message; + saves |
| H13 | Damaged config | Truncate `global.json` on a PC, relaunch | Main menu warns that the backup was loaded; previous mapping active |
| H14 | Per-game profile | Create `games/test/game/`, Library → X on it, change a binding, + | `config/controller/games/test.json` written; global unchanged |
| H15 | HOME and back | Hold ZR in Controller Test, press HOME, return | No `skip` stuck active; buttons held during HOME are ignored until released |
| H16 | Sleep / resume | Sleep the console for more than 1 minute, wake | Loader responsive, log shows "resumed", no stuck input |
| H17 | Controller reconnect | Detach and reattach Joy-Con; connect a Pro Controller | Log shows "controller change"; input works after each change |
| H18 | Docked | Dock the console | Display mode switches to Docked; log shows it |
| H19 | Exit | Main menu → Exit | Back to hbmenu; log ends with "exiting normally" and "log closed" |
| H20 | SD card loses writes | Lock or remove write access (e.g. fill the card) | Red "not writable" warning; the loader keeps running |

## Milestone 3 tests (embedded Python runtime)

Setup: copy `dist/renpy-runtime.nro` to
`sdmc:/switch/renpy-loader/runtimes/renpy-runtime.nro`.

| # | Test | Steps | Expected |
|---|---|---|---|
| H21 | Runtime installed | Loader → Diagnostics | "Runtime: … found" |
| H22 | Self-test, applet mode | Start the loader from the album, Diagnostics → A | The loader exits, the runtime prints the PASS lines and "0 required check(s) failed". Report at `cache/python-selftest.txt`; `logs/runtime.log` shows "Python 3.12.8 … initialised" |
| H23 | Self-test, application mode | As H22, but start the loader through title override (hold R on a game) | Same as H22. Record the memory line (used / total MiB) in both modes |
| H24 | Return to loader | After H22, press + | The loader starts again and shows the main menu |
| H25 | HOME and sleep during the self-test | While the PASS lines print, press HOME or sleep, then return | The test finishes; no hang; the status lines are printed |

Record the full report text and both memory figures; they set the memory budget for
Milestone 4.

## Milestone 4 tests (Ren'Py engine)

Setup:
- Install the Milestone 3 files, with the new `renpy-runtime.nro`.
- Copy the `the_question` folder from the official Ren'Py 8.5.3 SDK (renpy.org) to
  `sdmc:/switch/renpy-loader/games/the_question/`.
- Unless a test says otherwise, start the loader through title override (application mode).

| # | Test | Steps | Expected |
|---|---|---|---|
| H26 | Cold launch | Library → the_question → A | The Ren'Py main menu within about 30 s. `logs/the_question/log.txt` shows "Initializing gles2 renderer" and the GPU vendor |
| H27 | Dialogue and choices | Start, advance with A, reach the first menu, choose with the D-pad + A | Text renders correctly; the choice is taken |
| H28 | Controls | Try L, R, X, Y, B, +, −, ZL, hold ZR | Rollback, skip, hide UI, history, back, game menu, quick menu, auto-forward, hold-to-skip |
| H29 | Virtual cursor and touch | Move the right stick, press it on a button; tap a button on the touchscreen | The arrow cursor moves smoothly and can click; touch works |
| H30 | Audio | Listen through the opening | Music and sound effects play, no stutter |
| H31 | Save / load | Save to slot 1, go back to the library, launch the game again, load slot 1 | The same scene is restored; the save is in `saves/the_question/` |
| H32 | HOME and sleep | Press HOME mid-dialogue, return; sleep for more than 1 minute, wake | The game resumes; audio and video return; no stuck skip; no dialogue skipped |
| H33 | Back to the library | Main menu → Quit | The loader comes back |
| H34 | Applet mode | Start the loader from the album, Library → the_question → A | The loader refuses with a message (full memory needed, hold R); the game does not start, the console does not crash |
| H35 | Emergency combo | In game, hold + and − for 3 s, then quit to the loader | The "controls reset to default and saved" notice; the loader uses the defaults too; `runtime.log` shows "emergency reset saved to …global.json: ok" |

Record `logs/the_question/log.txt` and `logs/runtime.log` for every result.

## Milestone 7 tests (graphical loader, covers, Change picture)

Setup:
- Push `dist/renpy-loader.nro` with `scripts/switch-ftp.sh HOST:PORT push-loader`; the script
  reads the file back and compares it.
- At least one game with pictures and one without, so a real cover and a placeholder both
  appear. Pictures come from the importer: re-export with `--replace`, or *Change cover* in
  the desktop window, both with `--out ftp://HOST:PORT`.
- Use application mode (hold R while starting a game) for every test that launches a game.

| # | Test | Steps | Expected |
|---|---|---|---|
| H36 | Title and first line | Start from hbmenu | hbmenu shows "rpynx"; `loader.log` starts with "rpynx 0.1.0, …" |
| H37 | How to play | First start, no seen marker in `config/loader.json` | The guide opens by itself; after leaving it, the next start goes straight to the library |
| H38 | Covers in the library | Open the library | Games with `cover.jpg`/`cover.bmp` show the cover; the others show a tinted placeholder with the title's first letter; one cover is filled per frame after the first, and launching does not wait |
| H39 | Row layout | Move the focus along the row | The focused cover is 12% larger, nothing is cut off |
| H40 | Grid layout | + menu → Layout → Grid | Every cover is drawn; scrolling stays smooth |
| H41 | Layout page | + → Layout, switch to Grid, then back to Row | Only Row and Grid are offered, "in use" follows the selection, it survives a restart (`config/loader.json`) |
| H42 | Game details | Press A on a cover | Big cover, a long note that scrolls, three buttons at the bottom right: Change picture, Controller settings for this game, Launch |
| H43 | Back from Layout | + menu → Layout, then press B | Back to the library, not the + menu |
| H44 | Picture page | Details → Change picture | Album screenshots newest first, "3 of 11" and the capture time; the 2:3 frame is full height, the rest is shaded; videos are not among them |
| H45 | Move the frame | D-pad and stick, then drag the frame with a finger | Both move the frame; the touch drag follows the finger and stops at the picture's edges |
| H46 | Use the picture | Press A on the framed screenshot | Writes a 300×450 `cover.bmp`; `manifest.json` gets `"cover_custom": true`; the library shows the new cover at once |
| H47 | Game without a manifest | Copy a game folder into `games/` by hand, Change picture on it | A message; no picture and no manifest are written |
| H48 | Memory | Note the heap before, during and after the picture page | The 3.7 MB screenshot buffer is gone after leaving; `loader.log` shows the Album service closed |
| H49 | Launch still works | Library → a game → Launch, then quit | The game starts, the loader comes back (regression check after the app/ui split) |
| H50 | Numbers | Read the `ui` summary line after a minute | First frame, frame time, full-screen redraws, core load, SoC temperature, heap, cover count; compare with 2026-10-22: 80 ms, 11–17 ms, 36–38 °C, 20–24 MB |

Fetch logs with `scripts/switch-ftp.sh HOST:PORT logs`, and record the `ui` line for every
result. LVGL logs one warning per JPEG decode because it reads past the last MCU row; the
picture is complete. A cover line about a progressive JPEG or an oversized picture is a real
failure.

## Transfer tests (add games over Wi-Fi, the loader's own FTP server)

Setup: the Switch and the computer on the same Wi-Fi; a game folder or archive on the
computer that the Switch does not have yet, preferably a few hundred MB so the speed means
something. See "Add games over Wi-Fi" in `docs/LOADER_UI.md`.

| # | Test | Steps | Expected |
|---|---|---|---|
| H51 | Page | + menu → Add games over Wi-Fi | The address in large type (`IP:5000`), "waiting for the importer"; `loader.log`: `ftp: serving on …`. With Wi-Fi off (airplane mode): "no Wi-Fi", the status bar suggests connecting |
| H52 | Send a game | In the importer window, *Connect to the Switch over Wi-Fi*, enter the address, *Connect*, then *Export* | Both progress bars move: the window's and the Switch's ("X / Y MB"); the Switch shows the game's id and each file; at the end "done", "<id>, received" and the pop-up "<title> arrived": *Go to the library* returns to the library, *Send another game* (or B) closes it |
| H53 | Back to the library | Press B (done) | The new game appears in the library with its cover; `loader.log`: `ftp: stopped: …` and a new `library scan` |
| H54 | Replace and delete | Export the same game again with *Replace*, then delete it from the window's library | Saves are kept, no `.uploading-`/`.old-` folders remain; after B the deleted game is gone from the library |
| H55 | Speed | Read `ftp: … sent … (Z MB/s; W MB/s while receiving)` | Note the MB/s and compare with sphaira's 1–2 MB/s; if lower, try raising `SOCK_BUF`/`FILE_BUF` in `ftpd.c` |
| H56 | Leave mid-transfer | Start a large export, press B on the Switch halfway | The page closes within a second; the importer reports the lost connection and deletes `.uploading-<id>`; the library shows nothing half-sent |

## System-language tests (loader, runtime and importer)

Setup:
- The loader and runtime installed (the Milestone 3 and 4 files).
- In **System Settings → System → Language** on the Switch, switch languages in turn;
  restart the loader after each change.
- The desktop importer can override the language with the `RSL_LANGUAGE` environment
  variable (`zh-CN`, `zh-TW`, `en`), to check it on a computer whose system locale is not one
  of these.

These rows have **not been run**. Until a real-console result exists, the "system language"
row in `docs/COMPATIBILITY.md` stays hardware-unverified; the host test only covers the
language-tag-to-catalog mapping and fallback (`test_system_locale_mapping_and_catalogs` in
`tests/unit/test_mtp.py`), not font rendering or layout on the console.

| # | Test | Steps | Expected |
|---|---|---|---|
| H57a | Simplified Chinese UI | Set the system language to Simplified Chinese, start the loader | Main menu, + menu, Diagnostics and notices are in Simplified Chinese; Chinese glyphs come from the system shared font, no boxes or mojibake; `loader.log` is normal |
| H57b | Traditional Chinese UI | Set the system language to Traditional Chinese, start the loader | The UI is in Traditional Chinese (`locale_hant.inc`) with correct glyphs; the same flow as H57a |
| H57c | English fallback | Set the system language to an untranslated one (e.g. Japanese or French), start the loader | The UI falls back to English; no missing glyphs or crash |
| H57d | Runtime notice language | Under H57a's language, start a game and trigger a runtime notice (hold + and − for 3 s for the emergency reset, or open a game with no history screen) | The runtime notice (`runtime/renpy/boot/rsl_locale.py`) matches the system language; loading and saving `config/controller/global.json` is unaffected by language |
| H57e | Importer language | On the desktop, run the importer under a Simplified Chinese, Traditional Chinese and English system locale; then override with `RSL_LANGUAGE` | Window, report and error text follow the system or the override; other languages fall back to English; `RSL_LANGUAGE` does not affect the language inside a game |
| H57f | Game language stays independent | Under H57a's language, start a game with its own language choice (e.g. The Question's preferences) | The game language stays under the game's own control; the loader's system language does not override the game's setting |

## USB MTP tests (add games over USB)

Setup:
- The loader updated to a build with `platform/transfer/mtp_usb.c`.
- A **data-capable** USB cable (charge-only cables do not enumerate the device); the computer
  connected directly to the Switch or through a data hub.
- On the computer: the packaged Windows importer uses the system WPD service; macOS/Linux
  need `libmtp` (macOS: `brew install libmtp`); no other MTP program may claim the device.
- A game folder or archive, and a new version of the same game for the replace test.

Until these rows have a real-console PASS, the MTP row in `docs/COMPATIBILITY.md` stays
hardware-unverified. Host-side coverage: export, replacement, cancel, the MTP wire protocol
(`build/host/mtp_host`) and the UI language mapping, in `tests/unit/test_mtp.py`; none of it
counts as hardware verification.

| # | Test | Steps | Expected |
|---|---|---|---|
| H58 | Open the USB page | + menu → *Add games over USB*, connect the computer with a data cable | The page shows "USB MTP"; `loader.log`: `mtp: USB transfer ready`; the computer enumerates an MTP device named `rpynx` within seconds (USB class 6/1/1), no system error dialog |
| H59 | Send a game from the importer | In *Target → Connect to the Switch over USB (MTP)*, *Find USB devices*, pick `rpynx`, *Analyse* and *Export* | The device is found; the Switch page shows progress (file and bytes); at the end the game is in the library with its cover and details from `manifest.json`; `loader.log` records the receive |
| H60 | Replace and keep saves | Export the same game again with *Replace*; confirm the library still shows it on the Switch | Saves stay in `saves/<id>/`; no leftover `.uploading-`/`.previous-`/`.old-` temporary objects; the library refreshes after B |
| H61 | Disconnect and errors | Pull the USB cable mid-transfer of a large file; reconnect and send again | The page shows MTP stopped and why; the importer reports the lost connection; the Switch does not crash; sending again works after reconnecting, and the half-sent game never enters the library |
| H62 | Large-file limit | Send a single file close to 4 GiB (optional) | At the MTP 32-bit container length limit the importer gives a clear error rather than silent corruption; SD card or FTP bypasses it. Record the actual limit |

Record `loader.log` and the importer output for every row; MTP log lines start with `mtp:`.

Later milestones add the gameplay tests from the project brief (new game, dialogue, choices,
save/load cycle, rollback, sleep during play, soak test).
