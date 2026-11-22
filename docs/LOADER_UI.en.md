# Loader interface (Milestone 7)

[简体中文](LOADER_UI.md) · **English**

The graphical game library that replaces the loader's libnx text console.

> The "Status (2026-10-25)" notes and the review rounds below are historical: they show how
> the interface evolved. The current implementation is the code. Where it matters this page
> adds the current state after the historical description (for example, the + menu gained a
> USB MTP page). Volatile numbers such as artifact sizes and the default layout follow
> `BUILD_STATUS.md` and the code.

Status (2026-10-25, historical):
- The design was approved; phase two was built; the first hardware test was done (screenshots and `ui` log lines).
- Second round, from user feedback on that test: fonts were measured so nothing is clipped; icons are centred with their text; covers are larger; library layout and accent colour get their own page; the "game guide" became an illustrated guide; menus slide in.
- Third round, from feedback on the Switch: the detail page keeps "Launch" (with a play icon) at the bottom and scrolls for the facts; layout settings and accent colour merged into one "Layout" page; every action also works by touch.
- Covers (2026-10-22): the importer generates covers from a game's menu art or a chosen image, and so does the desktop window; on the Switch, the detail page's *Change picture* turns an Album screenshot into a cover (see "Covers"). Hardware test 28 (2026-10-23) passed all the rounds above plus covers; afterwards the cinematic layout and the images behind the interface were removed.
- Add games over Wi-Fi (2026-10-25): the + menu page runs the loader's own FTP server for the desktop importer (see "Add games over Wi-Fi"). Verified on the Switch in test 29 on 2026-10-25.
- Builds with no warnings; `.nro` is 819 KB (historical; for the current size see `BUILD_STATUS.md`).
- Code: `app/launcher/` (main.c, actions.c, library.c, controls.c, art.c, transfer.c) and `app/ui/`: ui.c (screen frame, building and updating screens), ui_style.c (fonts, styles, accent colour), ui_widgets.c (primitives, touch), ui_art.c (covers), plus one file per screen group: ui_library.c, ui_detail.c, ui_picture.c, ui_controls.c, ui_pages.c.

First hardware run (handheld, application mode, 4 games, no covers):
- 80 ms from `main()` to the first frame;
- 1% core 0-2 usage when idle, SoC 36-38 °C;
- 11-17 ms average frame time, 20-42 ms for a full-screen redraw (fades, layout changes);
- 20 MB memory use.

## Library: LVGL 9.6.0

| | |
|---|---|
| Licence | MIT (`build/lvgl/lvgl/LICENCE.txt`). Bundled parts used: tiny_ttf (stb_truetype, MIT / public domain), TJpgDec (ChaN, free to use with attribution), Montserrat 14 (SIL OFL) |
| Version | v9.6.0, 2026-09-16, the latest release |
| Fetch | `third_party/lvgl/fetch.sh`: the GitHub tag tarball, SHA-256 pinned, as `runtime/python/build.sh` does for CPython. The tarball is 111 MB (with tests, docs, demos), so it is not committed; only `src/`, `include/`, `lvgl.h` and the licence are unpacked to `build/lvgl/lvgl` |
| Build | `third_party/lvgl/Makefile` produces `liblvgl.a` with any compiler (the devkitPro template cannot build LVGL directly: two LVGL files share a name) |
| Config | `third_party/lvgl/lv_conf.h`: only the parts that differ from LVGL's defaults. XRGB8888, libc malloc, a software renderer with 4 pixel formats, NEON on the Switch, no theme, only label / image / bar / canvas widgets |

## Rendering

- LVGL's software renderer draws on the CPU into the libnx framebuffer. No Mesa; the loader does not use the GPU.
- XRGB8888 is stored in memory as B,G,R,X, matching the Switch framebuffer's BGRA layout.
- Retained mode: a screen is built when `g.screen` changes. `ui_update()` changes only what differs from the last frame, so LVGL redraws just those regions. A frame with no change draws nothing.
- Text uses the console's own font (the shared font "Standard", from the `pl` service), rendered through tiny_ttf. The `.nro` contains no font files.
  - History: at first only Latin and Japanese worked; Chinese or Korean titles showed as boxes.
  - Current: the loader chains the matching Chinese shared font (`PlSharedFontType_ChineseSimplified` or `ChineseTraditional`) as a fallback, so UI Chinese and Chinese game titles both display (glyph coverage on hardware is still to be verified). Korean still has no fallback font.
  - Montserrat 14 is the default font and the fallback for LVGL's arrow symbols.

## Code structure

| Path | Purpose |
|---|---|
| `app/launcher/loader.h` | Loader state `g`: screen, focus, games and their manifest data, the controller settings being edited, console info. No libnx types |
| `app/ui/ui.c` | All screens, built from `g`. Two functions: `ui_init()` and `ui_update()` |
| `app/launcher/main.c` | Logic and libnx: scan, manifest, remapping, launch, framebuffer, input |
| `app/ui/ui_locale.c`, `locale_en.inc`, `locale_hant.inc` | UI text translation: Simplified Chinese is the source literal; English and Traditional Chinese are lookup tables (see "Interface language") |
| `tests/ui/mockups.c`, `scripts/mockups.sh` | Renders every screen with sample data on the Mac, through the same `ui.c` and `lv_conf.h`, under ASan/UBSan |

> The current code has split each screen into its own file: `ui_style.c`, `ui_widgets.c`,
> `ui_art.c`, `ui_library.c`, `ui_detail.c`, `ui_picture.c`, `ui_controls.c`, `ui_pages.c`
> (see [ARCHITECTURE](ARCHITECTURE.en.md)). `ui.c` is still the screen frame and dispatch
> entry point.

## Interface language

The loader reads the console's system language (`rsl_system_language()` in
`platform/locale/rsl_locale.h`):

- `zh-Hans` (Simplified Chinese): the Chinese literals in the source are the display text; the translation table is empty.
- `zh-Hant` (Traditional Chinese): uses `locale_hant.inc`.
- Other languages: use the English in `locale_en.inc`.

UI text goes through `rsl_ui_tr()`; widgets such as `label()` and `para()` translate when
created, and `set_textf()` handles strings with format arguments. When adding or changing UI
text, the three tables (source Chinese, English, Traditional Chinese) must keep the same
keys. For fonts, the loader chains the matching Chinese shared font (Simplified or
Traditional) as the text font's fallback, so the interface and Chinese game titles display;
a game's own language is still controlled by the game. The desktop importer's language
selection works the same way, see `docs/IMPORTER.en.md`.

## Screens

Design mockups: `scripts/mockups.sh` writes `build/host/mockups/*.png` (1280×720, the Switch resolution).

| Screen | Content |
|---|---|
| Library, grid layout | Five portrait covers per row, with titles, scrolls vertically |
| Library, row layout (default) | A bottom row of portrait covers, scrolls horizontally; the focused game's large white title (48 px) and its chips sit above, with no title bar and no game count. The focused cover scales up 12%. Mockups `1a`, `1b`, `1d` |
| Library, all layouts | Badges: red `!` (cannot launch), amber `!` (performance issues). Header: game count, handheld/docked mode, an amber "Applet mode" chip. A read-only SD folder or a controller file loaded from backup shows a red/amber banner. Empty library: add games over Wi-Fi, or how to copy games |
| Menu (+ in the library) | A panel sliding in from the right: add games over Wi-Fi, add games over USB (MTP), controller settings, controller test, layout (subtitle: the current layout and accent colour), game guide, diagnostics, exit to Homebrew Menu. Scrolls when the entries do not fit; the help (A, B) sits below a divider at the bottom. Moving focus scrolls every list it is in; on the first row of a list it scrolls to the very top to show its heading |
| Game guide | Shown once on first launch, then openable from the + menu. A scrollable (up/down) illustrated guide with five cards: in-game buttons (from the user's current mapping), start with full memory, adding games, personalisation (layout and accent), and buttons not responding? (the emergency combo). Each card has a small drawing. A or B closes it |
| Layout (+ menu) | A scrollable page. Library: row and grid side by side, each card split in two, a white wireframe preview on the left and the name and short description on the right, with "In use" on the current one; A selects the focused layout. Colour: eight presets and a custom hue slider (left/right, live preview); within 25° of the error red's hue, a warning appears beside the heading. The page follows the new accent live, without a rebuild. B returns to the library and closes the menu, as on every page opened from the + menu. Mockups `11`, `12a`, `12b`, `12c` |
| Game details (A on a cover) | Large cover; title, folder, Ren'Py version, status, runtime and whether it is installed; the note for a recompiled Ren'Py 7 game, why a game cannot launch, and up to three performance issues; scrolls up/down when the facts overflow. Buttons stay at the bottom right: Change picture, this game's controller settings, then Launch with a play icon ("Cannot launch" when there is a problem). Mockups `3`, `3b`, `3c`, `4` |
| Picture (the detail page's *Change picture*) | Shows one Album screenshot at a time, newest first, with "3 of 11" and the capture time. A frame (2:3, full height) marks the part that will become the cover; the rest is dimmed. A saves it as the game's cover. Mockup `13a`; results `13c`, `13d` |
| Applet-mode notice | Launching in applet mode (the loader opened from the Album) starts nothing. The notice explains why (limited memory; games crash the whole console in this mode, hardware tests 8 and 26) and how to start with full memory (close the loader, hold R when opening a game). One button, "OK"; there is no "launch anyway". The library header keeps the amber "Applet mode" chip |
| Controller settings | Global or per game. One row per action, its buttons shown as chips, conflicts in red; assigning shows "Press a button ... 5 s"; then dead zone, cursor speed and acceleration, repeat delay and interval, sticks, cursor visibility. Header: configuration, presets, Unsaved |
| Controller test | Buttons laid out like the real controller (shoulders on top, the D-pad and A/B/X/Y in a cross, sticks in the middle), lit in the accent colour when pressed; shows logical actions, touch, the cursor and the last 6 events |
| Diagnostics | Applet type, display, memory bar, SD folder, controller file, runtime; the recent log; a "Run Python self-test" button |
| Add games over Wi-Fi (+ menu) | The address to type into the importer (`192.168.1.20:5000`, shown large), instructions, then the server state (waiting, connected to the computer, stopped), the game being received with a progress bar ("183.4 of 412.0 MB", then "Done"), the files, and the total files and megabytes received. When the importer moves a game into place, a "<title> received" dialog appears with *Send another game* and *Go to the library* (focus on the latter; left/right to choose, B closes). The same message stays on the page as a green note until the next upload begins, hiding the instructions. Without Wi-Fi it shows "No Wi-Fi", with a status-bar hint to connect in the system settings. B (Done) stops the server and returns to the library, re-read |
| Add games over USB (+ menu) | Shares the transfer-page frame with the previous page, but the title is "USB MTP" and the text explains connecting a data cable, finding and selecting the `rpynx` device in the importer, and leaving the page after the transfer. The server state, progress bar, file list and "<title> received" notice are the same. Provided by `platform/transfer/mtp_usb.c` (libnx USB service) and `mtp_core.c` (protocol core), exposing only the `switch/renpy-loader/` library. Switching to this page sets `g.transfer_usb`, and `ui.c` swaps the page title accordingly. Implemented in source and passing host protocol regression; actual USB transfer on the Switch is still to be verified on hardware (see `BUILD_STATUS.md`) |

The status bar (footer left) shows the last logical message: red for errors, green for
success. A screen with no message shows a hint there instead (controller settings: the
emergency combo).

## Navigation

Every button goes through the mapping engine (`platform/input`), so remapping also applies to
the loader. The help line shows the button currently mapped to each action and updates
immediately after a new mapping is saved.

| Action (default button) | Library, row | Library, grid | Details | Settings | Test | Diagnostics | Menu / dialogs |
|---|---|---|---|---|---|---|---|
| Left / right (D-pad, stick) | Previous / next game | Move in the grid | Move between buttons | Adjust a value | - | - | - |
| Up / down (D-pad, stick) | - | Move in the grid | Scroll the facts | Rows | - | - | Move (menu) |
| Confirm (A) | Details | Details | Press the button | Assign / toggle | - | Self-test | Select / OK |
| Back (B) | - | - | Library | Back (twice if unsaved) | Hold 1 s | Library | Close |
| hide_ui (X) | Game controls | Game controls | - | Restore defaults | - | - | - |
| history (Y) | - | - | - | Next preset | - | - | - |
| game_menu (+) | Menu | Menu | - | Save | - | - | Close menu |

- Every screen focuses its first item when it opens. Returning from game details, the library keeps focus on the previous game.
- Row and grid stop at the edges; lists wrap, like the console's own UI.
- The game guide opens automatically on first launch; `config/loader.json` records that it was seen (`"help_seen": true`).
- `config/loader.json` holds the loader's own settings: `{"library_layout": "row", "accent": "#ff5fa8", "accent_hue": 330, "help_seen": true}`; `library_layout` is `row` or `grid`.
- Library layout: row by default (the layout the user described first). The + menu toggles it; it is stored in `config/loader.json` (`{"library_layout": "row"}`, written atomically like the controller files), not in the controller file, whose parser rejects unknown keys.
- A held direction repeats with the user's own repeat delay and interval.
- Picture page: left/right switch to a newer/older screenshot, L/R (rollback/forward) move the cover frame by 80 px, and dragging a finger over the screenshot does the same (the frame's centre follows the finger), confirm saves the framed part as the game cover, back returns to details. A screenshot is loaded only after focus rests on it for 120 ms.
- Holding + and − together for 3 seconds anywhere: restores default controls and opens controller settings (unchanged).
- Touch: the touch screen is an LVGL pointer (`main.c`). Dragging scrolls every list and row; on the picture page it moves the cover frame; tapping an item focuses it and confirms, like pressing A (guide cards only take focus); tapping the footer hint runs its action, and for a two-button hint (← →) runs the second button; tapping beside the + menu or the applet notice closes it. The controller test page needs B held to exit, so tapping its hint there exits at once. `tests/ui/mockups.c` checks taps on "Launch" and the back hint.

Why there is no LVGL keypad input device: the mapping engine already turns buttons into
actions and uses the user's repeat timing. A keypad device would add LVGL's own key repeat on
top, and it has no keys for hide_ui, history or game_menu. The logic in `main.c` moves
`g.sel` on each action event, like the console's own UI, and `ui.c` shows it as focus. Either
way the requirement holds: no button reaches the interface without going through the user's
mapping.

## Appearance

OLED dark with a pink accent, as the user requested ("lebih dark mode OLED", "warna birunya
jadi warna pink").

- **Background:** pure black `#000000`. On an OLED Switch, black pixels are off.
- **Surfaces:**
  - cards and panels are near-black `#121215` with a 1 px hairline `#26262d` so their edges show against black;
  - rows, buttons and test labels are `#1f1f25`.
- **Text:** `#f0f0f3`; secondary text `#9b9ba4`, legible on black.
- **Focus:** a pink (`#ff5fa8`) outline, fading in over 120 ms (`FOCUS_FADE_MS`). Covers also scale: 4% in the grid, 12% in the row.
  - Focus text is pink too: menu entries, settings rows, buttons, the focused game's title.
  - A focused row or button is filled with deep pink: 10% of the accent mixed into `#121215`. Stronger fills (20% and 40% into `#1f1f25`) were tried and rejected: the pink text disappears into them.
  - The same pink lights the controller-test labels and the configuration chips.
  - Disabled buttons ("Cannot launch") stay grey even when focused.
- **Accent setting:** any colour; eight presets (default pink, blue `#25d0f2`, purple, indigo, teal, lime, sky blue, white), or a custom hue, chosen on the "Layout" page and stored in `config/loader.json` (`"accent": "#ff5fa8"`, `"accent_hue"`).
  - Only the accent changes. The background, cards, text, amber, green and orange-red error colour are the same either way, so errors stay distinct from focus.
  - In `ui.c` the accent is a single value chosen by the settings (`ACCENT`), not a second palette.
  - Mockup: `1d-library-row-blue.png`.
- **Status colours:**
  - errors are orange-red `#ff4b2b`, kept apart from the pink so "selected" and "has a problem" do not look alike (a focused game with a red `!`, a focused conflict row);
  - amber `#ffb547`, green `#52d48a`.
- Covers have rounded corners (20 px) baked into the decoded thumbnail as transparency (ARGB, edge pixels partly transparent), so the focused cover's outline stays intact. Baked as black, it would cover the middle of the outline's arcs (hardware test, 2026-10-22). Cards, rows and buttons use 14 / 10 / 12 px.
- Chips ("Experimental", "Ren'Py 8.5.3", "Handheld", "Missing", configuration and preset chips, banners) share one style: a full rounded capsule (`LV_RADIUS_CIRCLE`), 16 px × 7 px padding, the text in its colour, the background a 20% tint of it.
- **Controller buttons:** on the Switch, the console's own button icons, the ones the HOME menu, hbmenu and sphaira use.
  - They come from the system shared font "Nintendo Extended" (`plGetSharedFontByType(PlSharedFontType_NintendoExt)`), drawn through tiny_ttf like the text font.
  - The font is loaded from the console at runtime, as hbmenu and sphaira do. No Nintendo content is copied into the `.nro` or the repository.
  - The code points match those sphaira's `nvg_util.cpp` uses:

    | Button | A | B | X | Y | L | R | ZL | ZR | ↑ | ↓ | ← | → | + | − | L-stick press | R-stick press |
    |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
    | Code point | E0E0 | E0E1 | E0E2 | E0E3 | E0E4 | E0E5 | E0E6 | E0E7 | E0EB | E0EC | E0ED | E0EE | E0EF | E0F0 | E104 | E105 |

  - hbmenu's dark theme uses a solid icon set for A, B, X, Y (E0A0 to E0A3) and + and − (E0B3, E0B4). Choose the icon set on hardware.
  - SL, SR and stick directions keep drawn labels, which are also the fallback when the shared font cannot load.
  - The Mac mockups use drawn labels (dark glyphs on light discs) instead: Nintendo's font is not on the Mac and is not copied here.
- Fonts: 18 / 22 / 28 / 36 / 48 px. The console font reports a line box that is too tight for its own glyphs (in the first hardware run, descenders were cut and wrapped lines overlapped), so `fit_lines()` measures real glyphs (Á, g, j, 夏) and sets each size's line height from them, with some air. Body text has 8 px line spacing.
- Button icons are 26 px and centred in their line height (`icon_font()`), so they line up with the text beside them. Stick directions show the stick icon; D-pad arrows are the console's own icons. No LVGL symbol glyphs are used: the console font returns blank boxes for their code points.
- Effects are all light: focus transitions, a 150 ms fade between screens, 120 ms fades for menus, dialogs and the status line, and an animation scrolling to the focused item.

## Covers

Portrait 2:3, like the box art in GOG Galaxy, used by both library layouts. The user first
chose a 16:9 card taken from the main-menu art, then asked for portrait covers like GOG.
They also asked for a custom-cover option in the desktop app.

The difficulty: Ren'Py games do not ship covers, only 16:9 menu art. The automatic cover
scales that image to fit entirely, centred in the 2:3 frame, and fills the remainder with a
blown-up, blurred copy of the same image (reworked 2026-11; previously it was the middle
third of the image, which often showed part of the background rather than the game's title).
A custom cover helps, for example a 600×900 image from SteamGridDB or a Steam capsule.

- **Manifest:** `"cover": "cover.jpg"`, a path relative to the game folder; `"cover_custom": true` when the user chose it (the importer's `--replace` keeps it).
- **Files:**
  - a baseline JPEG, 300×450 (the detail-page size), written by the importer;
  - or a 24-bit BMP of the same size, written by *Change picture* on the Switch (`cover.bmp`);
  - the loader also accepts other sizes up to 600×900, cropping other aspect ratios to the middle (the importer's `cover.jpg` is already 2:3, so no crop is needed);
  - a progressive JPEG, a larger image or an unreadable file shows the placeholder and logs one line in `loader.log`.
- **Placeholder:** a cover tinted from the folder name, with the title's initial. Used for games without a cover, such as manually copied games.
- **Cost on the Switch:**
  - LVGL's JPEG decoder (TJpgDec) keeps nothing between draws, so each cover is decoded once and scaled to a 220×330 thumbnail (290 KB; about 19 MB for 64 games);
  - all layouts use the same thumbnail. The row scales that thumbnail to enlarge the focused cover by 12%;
  - once the library first appears, one cover is decoded per frame, so start-up does not wait for covers;
  - the detail page decodes its 300×450 cover when it opens;
  - LVGL's decoder logs one warning per decode when it reads past the last row block. The images are complete.

### Importer side (2026-10-22 build; docs/IMPORTER.en.md "Covers")

- **Automatic cover:**
  - when `gui.main_menu_background` in `gui/gui.rpy` is a plain string literal, read it; otherwise use `gui/main_menu.png`. The image is fitted whole into the cover (see above);
  - the image may be a loose file or inside an `.rpa` archive (`rpa_index()` can already read the index);
  - not found or unreadable: no cover, with an extra report line. The placeholder remains.

- **Custom cover:** `--cover IMAGE`, and *Change cover…* in the desktop window, on the import page and on games already on the SD card or Switch. It overrides the automatic cover and goes through the same conversion. The manifest marks `"cover_custom"`, and an export with `--replace` keeps it (including a cover chosen on the Switch).
- **Conversion:**
  - uses ffmpeg, which the importer already finds on macOS, Linux and Windows (`find_tool(FFMPEG)`, `--tools`). No new dependency, and no macOS-only tool such as `sips`. Apart from this, the importer uses only the Python standard library;
  - `scale=300:450:force_original_aspect_ratio=increase,crop=300:450`, single frame, `yuvj420p`: a baseline JPEG;
  - PNG, JPEG and WebP sources all work through ffmpeg;
  - without ffmpeg: no cover, and the report says ffmpeg is needed to make one.

### Switch side: Change picture (2026-10-22 build, verified in Switch test 28)

The user's choice (2026-10-22): pick an image from the Album, save it as BMP, add a third
button to the detail page, and make the cover frame movable.

- **Source:** the Album, through the system's Album service (`capsa`, libnx): screenshots on the SD card and in system memory, newest first. `capsaLoadAlbumScreenShotImage` returns decoded 1280×720 RGBA, which the loader converts in place to XRGB8888.
  - Why not read the files under `Nintendo/Album/` directly: every screenshot checked (11 from the user's Switch) is a baseline JPEG with 4:4:0 subsampling (Y 1×2), which TJpgDec rejects (it reads only 4:4:4, 4:2:2 and 4:2:0). The Album also holds system-memory screenshots.
  - Video is not included.
- **Storage:** the loader has no JPEG encoder, so it writes a 24-bit bottom-up BMP (`rsl_write_bmp`, `platform/filesystem/fs.c`): a 300×450 `cover.bmp` (405 KB) from the chosen part. LVGL's BMP decoder (`LV_USE_BMP`) reads it through the same `render_cover()`. The desktop window can show BMP too.
  - Written with `rsl_write_atomic`, image first, then `manifest.json` (cJSON): `"cover": "cover.bmp", "cover_custom": true`. If the manifest cannot be written, the game keeps its old cover.
  - A game without a `manifest.json` (copied by hand) shows a message instead: the manifest names the cover, and a manifest missing the importer's fields stops the game launching.
- **Memory:** the screenshot buffer (3.7 MB) exists only while the page is open. The page frees it before the 150 ms cross-fade to the next screen, and closes the Album service when leaving the page (including after the emergency combo).
- **Afterwards:** the game's thumbnail is decoded again (`ui_art_changed`), so the library shows the new cover immediately.

## Add games over Wi-Fi (2026-10-25 build, verified in Switch test 29)

The loader's own FTP server, so games can be sent from the desktop importer without leaving
the loader for sphaira. The importer needs no change: as with sphaira's server, `/` is the SD
card, so the same `ftp://HOST:PORT` path works for both.

- **Server:** `platform/transfer/ftpd.c`, using only plain POSIX sockets and stdio, running in its own thread: anonymous access, one client at a time, passive mode, port 5000 (the importer's default). Commands: the ones Python's `ftplib` and the importer use (USER, PASS, TYPE, PASV/EPSV, MLSD, CWD, RETR, STOR, MKD, RMD, DELE, RNFR/RNTO, ALLO), plus LIST, NLST, SIZE, PWD, CDUP for ordinary FTP clients.
- **`switch/renpy-loader/` only:** every path is resolved (including "..") and must stay within that directory, so nothing else on the SD card can be read or written; any other path returns 550. The client starts at `/`, as with sphaira (curl and `scripts/switch-ftp.sh` give paths from there); `/` and `/switch` can be entered but only list the path down to the next level. The server runs only while the page is open. The loader's own open file (`logs/loader.log`) cannot be read during this: the Switch filesystem refuses it (hardware, 2026-10-25).
- **Lifecycle:** opening the page reads the console's address (`nifm`), starts the socket and server, and stops the console sleeping (`appletSetAutoSleepDisabled`); leaving the page (B, or closing the loader from HOME) stops the server within 0.2 s even mid-transfer, then re-reads the library, whose covers are decoded again in the new order. Incomplete files are deleted.
- **Progress:** FTP does not say how much an upload will send, so the importer announces the game's size first with `ALLO <bytes>` (sphaira rejects it, which does no harm), and the bar counts from there. Moving `.uploading-<id>` into place marks the end ("Done"), so a video that got smaller after re-encoding does not leave the bar short. The importer window has its own progress bar.
- **Log:** `ftp: serving on …`; for every connection that sent anything, `ftp: … sent N file(s), X MB in Y s (Z MB/s; W MB/s while receiving)`, the second figure excluding listings, folders and idle time; `ftp: <id> received (<title>)`; `ftp: stopped: …`. Compare with sphaira's 1-2 MB/s. If it is slow, `FILE_BUF` and `SOCK_BUF` in `ftpd.c` are the tunables.
- **Test:** `tests/unit/test_ftpd.py` (run by `scripts/test.sh`) runs the server on the Mac under ASan/UBSan and drives it with the importer: export, replace (saves are moved over), delete, confirm nothing outside the jail is reachable, transfer a 5 MB file both ways, list, and stop while a client is connected.

## Phase two (built; running on the Switch since 2026-10-22)

> Historical section: the plan for when phase two landed and the `main.c` of that time. The
> current `main.c` has moved input navigation to `actions.c`, launch/chain-load to
> `library.c`, controller settings to `controls.c`, pictures and covers to `art.c`, and
> transfer to `transfer.c`; screen building is split across several files in `app/ui/`. The
> points below are kept as the design rationale of that time.

- `main.c`: keeps its logic (scan, the manifest table and its rejection rules, the applet-mode warning and confirmation, per-game controller settings, diagnostics, self-test, the emergency combo).
  - Replaces the `draw_*` functions with `ui_update()`.
  - Adds navigation between library ↔ details ↔ menu per the table above.
  - Status messages use friendly action names and real arrows, e.g. "History → X: also used by Hide dialogue" (now "history -> X conflicts with: hide_ui"). On the Switch, buttons can show their NintendoExt icons inline.
  - In applet mode, launching opens the notice but starts nothing. `main.c` already refused (no second-press override); the runtime refuses too. The Python self-test still runs in applet mode.
- Display: libnx framebuffer, LVGL in direct mode on its linear buffer.
- Fonts: the `pl` shared font.
- Build: the Makefile builds and links `liblvgl.a`. Check the loader `.nro` size and start-up time.
- Test: `scripts/test.sh` also renders every screen under ASan/UBSan.
- Guide: once the UI runs on the console, add a step-by-step "game guide" page with Switch screenshots under `docs/`.

### Light on the Switch

A UI bug must not keep the console busy or hot. Each item should stay small.

1. **No work when idle.**
   - With no input, no running animation and no cover waiting to be decoded, the loop does not render and does not call `framebufferEnd`.
   - It sleeps until the next input poll, as far as `lv_timer_handler()` allows, and only while `lv_anim_count_running()` is 0.
   - A frame is presented only when LVGL has refreshed a region.
   - Target: on the library screen, at most 5% core usage two seconds after the last input, measured the same way as the runtime telemetry (`IdleTickCount`).
2. **Regression test for redrawing every frame.** `scripts/test.sh` runs `tests/ui/mockups.c`. For every screen, and for both library layouts, it checks:
   - a second `ui_update()` with `g` unchanged invalidates nothing;
   - after the animations end, later refresh cycles refresh nothing.

   The test counts refresh calls, so it catches mistakes such as calling `set_text` with the same string, style resets and the like.
3. **Bounded cover memory.** The library holds at most `MAX_GAMES` (64) games, so thumbnails take at most about 19 MB, the same in all layouts.
   - Raising `MAX_GAMES` also means decoding only the visible rows plus the one before.
   - Until then, re-decoding while scrolling only adds stutter.
   - The exit log records peak heap (`mallinfo`).
4. **Logging** goes to `logs/loader.log`.
   - Every line starts with `ui`, so the log can be fetched over FTP (`scripts/switch-ftp.sh HOST logs`) and grepped.
   - `rsl_log()` flushes every line to the SD card, so log lines are rate-limited.
   - On start-up: the milliseconds from `main()` to the first frame, the applet type, the heap in use.
   - Every 60 seconds while the loader is open, never per frame:
     - frames drawn, average and maximum render milliseconds, full-screen frames;
     - core usage percentage, SoC temperature;
     - heap in use and peak, covers decoded and failed.
   - `log_telemetry()` (temperature, `IdleTickCount`, battery) moved from `runtime/main.c` to `app/diagnostics/procinfo.c`, used by both programs.
   - Warnings, at most one per kind per minute:
     - a frame over 33 ms, with the screen and the dirty region;
     - "redrawing while idle": frames still being drawn 2 s after the last input with no animation running, with the screen;
     - a cover that cannot be decoded.
   - LVGL's own log goes through the same function. TJpgDec's "read past the last row" warning is dropped; otherwise it would add a line per cover.
   - On exit: a summary line before `chain_load`, next to the existing heap audit.
5. **Host timing.** `scripts/mockups.sh` prints each screen's render time, labelled as host-only: it is not Switch timing.
