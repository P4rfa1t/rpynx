# Architecture

> This page records the layering, key decisions and pinned versions. The "Runtime plan
> (milestones 3–4)" section was written before implementation and is kept as a design
> record; the current state is the code, the build scripts and
> [BUILD_STATUS](../BUILD_STATUS.md).

## Layers

```
Switch physical input (libnx HID: npad buttons, analog sticks, touch screen)
        │  platform/input/input_switch.c        the only input file that depends on libnx
        ▼
device input normaliser  rsl_normalize_npad()  Nintendo button names, never SDL names
        ▼
controller mapping engine  rsl_input_update()  bindings, dead zone, repeat/long press, cursor, emergency combo
        ▼
logical actions (confirm, back, rollback, skip, …)
        ▼
consumers: the loader UI, and the Ren'Py bridge (runtime/renpy/boot/rsl_bridge.py)
```

The mapping engine (`platform/input/input.c`, `input_config.c`) and the filesystem helpers
(`platform/filesystem/fs.c`) are portable C with no libnx dependency, so the host test suite
(`scripts/test.sh`) runs exactly the code that ships in the `.nro`. The libnx bit layout the
normaliser relies on is checked against libnx's own constants by `_Static_assert` in
`input_switch.c`, so a libnx change breaks the build instead of silently swapping buttons.

| Path | Purpose |
|---|---|
| `app/launcher/` | Loader: `main.c` start-up, display, main loop, `ui` log lines; `actions.c` input and navigation; `library.c` game scan, manifest, settings, launch; `controls.c` remapping; `art.c` covers and Album-screenshot covers; `transfer.c` FTP over Wi-Fi and MTP over USB (`launcher.h`, `loader.h`) |
| `app/ui/` | Loader screens: LVGL 9.6 on the libnx framebuffer (`docs/LOADER_UI.md`): `ui.c` screen frame, building and updating screens; `ui_style.c` fonts, styles, accent colour; `ui_widgets.c` primitives, touch; `ui_art.c` covers; `ui_library.c` library layouts, + menu; `ui_detail.c` game details; `ui_picture.c` change picture; `ui_controls.c` controller settings, test, diagnostics; `ui_pages.c` game guide, layout and transfer pages; `ui_locale.c` and `locale_*.inc` UI text (`ui_internal.h`) |
| `app/diagnostics/log.c` | Line-flushed log at `sdmc:/switch/renpy-loader/logs/loader.log`, plus an in-memory tail buffer |
| `platform/input/` | Normaliser, mapping engine, JSON persistence, libnx glue |
| `platform/filesystem/` | Atomic writes with a `.bak` fallback, bounded reads, `mkdir -p` |
| `platform/transfer/` | `ftpd.c` the loader's own FTP server; `mtp_core.c` protocol core; `mtp_usb.c` libnx USB service |
| `platform/locale/rsl_locale.h` | Reads the UI language from the system settings (`zh-Hans` / `zh-Hant` / English fallback) |
| `config/profiles/` | Controller configuration samples (the same as the built-in presets, checked by tests) |
| `third_party/cjson/` | cJSON 1.7.18, MIT, bundled as-is |
| `third_party/lvgl/` | LVGL 9.6.0 (MIT): pinned fetch script, `lv_conf.h`, library Makefile; the source unpacks to `build/lvgl/` |
| `tests/unit/` | Host-side unit tests |
| `scripts/` | `build.sh` (pinned container build), `test.sh` (host tests) |

| Path | Purpose |
|---|---|
| `runtime/main.c` | Runtime `.nro`: embedded CPython; runs a Ren'Py game (`--game`) or the self-test script |
| `runtime/python/` | Static CPython 3.12.8 cross build: patches, `config.site`, stdlib packer, self-test ([README](../runtime/python/README.md)) |
| `runtime/renpy/` | Ren'Py 8.5.3 static build, engine staging, boot scripts and controller bridge ([README](../runtime/renpy/README.md)) |
| `runtime/compatibility/` | Replacement engine modules for features the Switch cannot support (assimp) |
| `tests/integration/renpy_desktop/` | Desktop test: official Ren'Py SDK + our boot/bridge with scripted controller input |
| `docker/Dockerfile` | Pinned devkitA64 image, plus the host headers needed to build the host CPython |

## Process model

The loader and the runtime are two separate `.nro` files. To launch a game, the loader calls
libnx `envSetNextLoad(runtime, "<runtime> --return <loader> --game <games>/<id>")` and exits.
The Homebrew loader then starts the runtime as a fresh process. On exit the runtime calls
`envSetNextLoad(<loader>)`, which brings the user back to the game library.

Why:
- Every game gets a clean process and address space. SnepOMatic reported errors when
  Mesa/nouveau initialise twice inside one process.
- A crashing game does not take the loader's state with it.
- Choosing a runtime per game is just another file in `runtimes/`.

The cost is a few extra seconds of `.nro` loading per launch. Not yet measured.

The runtime also accepts an optional `--savedir <dir>` (default `saves/<id>`), and points
`RSL_LOGDIR`, `HOME`, etc. at locations under `sdmc:/switch/renpy-loader/`. Without
`--game` it runs the self-test script.

Directories from the historical plan (`importer/`, `platform/graphics`, …) were introduced
milestone by milestone. `importer/` and `platform/transfer/` (FTP, MTP) have landed;
`platform/graphics` was not used — the loader does not use the GPU, see
[LOADER_UI](LOADER_UI.md).

## Key decisions

**Input does not go through SDL's controller layer.** SDL's GameController API names buttons
by position (Xbox layout), so Nintendo's A becomes SDL's "B". Reading libnx HID directly and
mapping by Nintendo name avoids that swap entirely. In the implementation, Ren'Py's own SDL
game-controller handling is disabled (`rsl_bridge` takes over the reads before
`controller.post_event`); otherwise every press would be delivered twice. The bridge converts
logical actions into Ren'Py events.

**Controller configuration is versioned JSON, loaded strictly.** Unknown actions or buttons,
wrong types, versions newer than the loader understands, and configurations that leave an
essential action (confirm, back, game_menu, overlay, up/down/left/right) unbound are all
rejected. Actions missing from the file keep their defaults, so new actions need no
migration. A rejected file falls back to `<file>.bak`, then to the defaults. Saving rejects
conflicting or incomplete mappings.

**The emergency recovery combo cannot be unmapped.** Holding + and − together for 3 seconds
is checked on the raw buttons, before mapping. It restores the default controls in memory and
opens the controller settings; the user saves explicitly. Limitation: a single detached
Joy-Con has only one of + / −, so it cannot perform the combo.

**Atomic writes.** Horizon's `rename` does not replace an existing file, so a save is:
write `x.tmp` → fsync → move `x` to `x.bak` → move `x.tmp` to `x`. Any interruption leaves a
readable `x` or `x.bak`, and the loader reads `x.bak` when `x` is missing or corrupt.

**Wake / reconnect.** On regaining focus, waking from sleep and a controller type change, the
engine drops every held action and ignores buttons still physically pressed until they are
released. This avoids stuck skip/rollback and repeated input after waking.

## Pinned versions

| Component | Version | Source |
|---|---|---|
| devkitA64 / libnx container | `devkitpro/devkita64:20260219`, index digest `sha256:1fc388c3…0282` | Docker Hub, arm64 + amd64 |
| cJSON | 1.7.18 | github.com/DaveGamble/cJSON tag v1.7.18 |
| LVGL | 9.6.0 (`third_party/lvgl/fetch.sh` pins the SHA-256) | github.com/lvgl/lvgl tag v9.6.0 |
| Ren'Py | 8.5.3 (`vc_version` 8.5.3.26051504, built and running) | renpy.org source tarball; sha256 from the PGP-signed `checksums.txt` |
| Cython | 3.2.4 (from Ren'Py 8.5.3's `uv.lock`) | PyPI sdist |
| CPython | 3.12.8 (what renpy-build uses for 8.5.3, built) | python.org source tarball, sha256 verified |

Package versions inside the container are recorded by `dkp-pacman -Q` in `BUILD_STATUS.md`.
Each build script verifies its sha256 before unpacking; the exact patches and module lists are
in the [CPython README](../runtime/python/README.md) and the
[Ren'Py README](../runtime/renpy/README.md).

## Runtime plan (milestones 3–4), from reading existing ports

> Historical section: the survey and trade-offs made before implementation, kept as a design
> record. The approach actually taken is in "As built" at the end of this section — largely
> the same as the plan below, with a few differences.

None of the existing ports is a clean starting point:

- **renpy-build has no Switch target.** Its master branch has moved to SDL3, which devkitPro
  does not package; 8.5.3 still uses SDL2 2.0.20, with pygame_sdl2 merged into
  `renpy.pygame`.
- **SnepOMatic/RenPy-Switch** (Ren'Py 8.5.3) is the newest port, but it has no licence file,
  so its code cannot be reused. It runs CPython 3.13 rather than 3.12, which forces
  workarounds in Ren'Py itself. Its general technique is public knowledge and will be
  reimplemented independently:
  - static CPython (`--disable-shared`, no dlopen/mmap, `Setup.local` marking every module
    `*static*`);
  - Cython-generated Ren'Py modules linked into the same ELF;
  - a `PyImport_ExtendInittab` table with dotted module names;
  - gl2 on switch-mesa GLES.
- **jvrcruzGAMES/RenpySwitch** has no licence (the upstream MIT was withdrawn), is one game
  per NRO, and has CPython 3.14 / 3.9 stdlib mismatches.
- **uyjulian/renpy-switch** (MIT, archived in 2022) targets Ren'Py 7.3.5 / Python 2.7 and is
  the reference for phase three.

Planned dependencies from devkitPro portlibs: switch-sdl2, switch-sdl2_image,
switch-sdl2_ttf, switch-mesa, switch-freetype, switch-harfbuzz, switch-libfribidi,
switch-ffmpeg, switch-libpng, switch-libjpeg-turbo, switch-libwebp, switch-zlib, switch-bzip2,
switch-liblzma. Not packaged by devkitPro and must be built here: CPython, and libffi (only if
ctypes is needed; the Ren'Py core does not need it).

**As built (summary of differences).** All four techniques above were adopted: static
CPython, Cython modules linked into the same ELF, a dotted-name inittab, and gl2/GLES. The
differences from the plan as landed:

- `ctypes` / libffi were not built, and `_ssl`, `_hashlib`, `_sqlite3`, `mmap` and
  `_posixsubprocess` are also unavailable; the effect on games is in the "Not available" table
  of the [CPython README](../runtime/python/README.md).
- Ren'Py's `renpy.gl2.assimp` and `renpy.gl2.live2dmodel` are excluded (no Switch port /
  proprietary); assimp is replaced by an empty Python implementation in
  `runtime/compatibility/`.
- Patch counts and contents follow the repository: six CPython patches and one Ren'Py patch
  (libhydrogen random source); see the two runtime READMEs.
- The loader does not use the GPU: LVGL software-renders to the libnx framebuffer (so
  `platform/graphics` was never created).
