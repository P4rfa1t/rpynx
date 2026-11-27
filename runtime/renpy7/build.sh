#!/bin/bash
# Build pygame_sdl2 + Ren'Py 7.8.7 for Switch (build/renpy7/dist).
# Builds inside container-local /tmp for fast I/O, keeping dl/ and dist/ in /src/build/renpy7.
set -euo pipefail

RENPY_VER=7.8.7
RENPY_SHA256=6f4968fccb473432276bbc1e3379ffe4ff765653f142ac15622ab7c720a09fdb
PYGAME_VER=2.1.0+renpy7.8.7
PYGAME_SHA256=8103b73e32604456db3950d4729982cd3243413a651014d06fc8196365ede37a
SDK_SHA256=cb42e32fecea6384fe7a6d1634d77da141acd8617ec4aff16ac4dc4a6f745339
CYTHON_VER=0.29.37
CYTHON_SHA256=f813d4a6dd94adee5d4ff266191d1d95bf6d4164a4facc535422c021b2504cfb

TOP=$(pwd)
B=$TOP/build/renpy7
HERE=$TOP/runtime/renpy7
WORK=/tmp/rpynx-renpy7-work
HOSTPY=$TOP/build/python2/host/bin/python2.7
PYINC=$TOP/build/python2/dist/include/python2.7
export JOBS=${JOBS:-$(nproc)}
[ -x "$HOSTPY" ] || { echo "run scripts/build.sh python2 first"; exit 1; }

mkdir -p "$B/dl" "$WORK/bin"

PYGAMETB=$B/dl/pygame_sdl2-$PYGAME_VER.tar.gz
[ -f "$PYGAMETB" ] || curl -fL -o "$PYGAMETB" "https://www.renpy.org/dl/$RENPY_VER/pygame_sdl2-$PYGAME_VER.tar.gz"
echo "$PYGAME_SHA256  $PYGAMETB" | sha256sum -c -

RENPYTB=$B/dl/renpy-$RENPY_VER-source.tar.bz2
[ -f "$RENPYTB" ] || curl -fL -o "$RENPYTB" "https://www.renpy.org/dl/$RENPY_VER/renpy-$RENPY_VER-source.tar.bz2"
echo "$RENPY_SHA256  $RENPYTB" | sha256sum -c -

SDKTB=$B/dl/renpy-$RENPY_VER-sdkarm.tar.bz2
[ -f "$SDKTB" ] || curl -fL -o "$SDKTB" "https://www.renpy.org/dl/$RENPY_VER/renpy-$RENPY_VER-sdkarm.tar.bz2"
echo "$SDK_SHA256  $SDKTB" | sha256sum -c -

CYTGZ=$B/dl/Cython-$CYTHON_VER.tar.gz
[ -f "$CYTGZ" ] || curl -fL -o "$CYTGZ" "https://files.pythonhosted.org/packages/source/C/Cython/Cython-$CYTHON_VER.tar.gz"
echo "$CYTHON_SHA256  $CYTGZ" | sha256sum -c -

PYGAME=$WORK/pygame_sdl2-$PYGAME_VER
rm -rf "$WORK/pygame_sdl2-"* && tar -xzf "$PYGAMETB" -C "$WORK"

RENPY=$WORK/renpy-$RENPY_VER-source
rm -rf "$WORK/renpy-$RENPY_VER-source" && tar -xjf "$RENPYTB" -C "$WORK"

for p in "$HERE"/patches/*.diff; do
    patch -d "$RENPY" -p1 --no-backup-if-mismatch -s <"$p"
done

CY=$WORK/Cython-$CYTHON_VER
rm -rf "$CY" && tar -xzf "$CYTGZ" -C "$WORK"

SDKARM=$WORK/sdk/renpy-$RENPY_VER-sdkarm
rm -rf "$WORK/sdk" && mkdir -p "$WORK/sdk"
tar -xjf "$SDKTB" -C "$WORK/sdk" \
    "renpy-$RENPY_VER-sdkarm/renpy/common" \
    "renpy-$RENPY_VER-sdkarm/lib/python2.7" \
    "renpy-$RENPY_VER-sdkarm/sdk-fonts"

# Host python-future + typing needed by Ren'Py 7.8.7 module/setup.py (converted .pyo -> .pyc in host_deps)
HOSTDEPS=$WORK/host_deps
rm -rf "$HOSTDEPS" && mkdir -p "$HOSTDEPS"
for pkg in future past builtins copyreg html http reprlib socketserver typing.pyo six.pyo; do
    if [ -e "$SDKARM/lib/python2.7/$pkg" ]; then
        cp -R "$SDKARM/lib/python2.7/$pkg" "$HOSTDEPS/"
    fi
done
find "$HOSTDEPS" -name '*.pyo' -exec sh -c 'mv "$1" "${1%.pyo}.pyc"' _ {} \;

# Cython wrapper callable both by PATH ("cython") and RENPY_CYTHON
printf '#!/bin/sh\nexec "%s" "%s/cython.py" "$@"\n' "$HOSTPY" "$CY" >"$WORK/bin/cython"
chmod +x "$WORK/bin/cython"
export PATH="$WORK/bin:$PATH"

# 1. Generate static Cython C sources for pygame_sdl2
(cd "$PYGAME" &&
    PYTHONPATH="$HERE/hoststub" PYGAME_SDL2_STATIC=1 PYGAME_SDL2_ANDROID=1 \
        "$HOSTPY" setup.py generate >"$B/pygame_generate.log" 2>&1) || { tail -40 "$B/pygame_generate.log"; exit 1; }
[ -f "$PYGAME/gen-static/pygame_sdl2.display.c" ] || { tail -40 "$B/pygame_generate.log"; exit 1; }

# Stage pygame_sdl2 headers for Ren'Py Cython/C builds
rm -rf "$WORK/pyinc/pygame_sdl2" && mkdir -p "$WORK/pyinc/pygame_sdl2"
cp "$PYGAME/src/pygame_sdl2/pygame_sdl2.h" "$WORK/pyinc/pygame_sdl2/"
cp "$PYGAME/gen-static/"*.h "$WORK/pyinc/pygame_sdl2/"

# 2. Generate static Cython C sources for Ren'Py 7.8.7
(cd "$RENPY/module" &&
    PYTHONPATH="$HERE/hoststub:$HOSTDEPS" RENPY_STATIC=1 RENPY_ANDROID=1 \
        RENPY_CYTHON="$WORK/bin/cython" RENPY_CYTHON_SINGLETHREAD=1 \
        "$HOSTPY" setup.py generate >"$B/renpy_generate.log" 2>&1) || { tail -40 "$B/renpy_generate.log"; exit 1; }
[ -f "$RENPY/module/gen-static/renpy.gl2.gl2draw.c" ] || { tail -40 "$B/renpy_generate.log"; exit 1; }

# 3. Cross-compile all native modules into librenpy7.a
source "$DEVKITPRO/switchvars.sh"
PKGS="sdl2 SDL2_image SDL2_ttf SDL2_mixer harfbuzz freetype2 fribidi libavformat libavcodec libavutil libswresample libswscale libpng libjpeg zlib"
export CC=aarch64-none-elf-gcc AR=aarch64-none-elf-ar
export CFLAGS="$CFLAGS $CPPFLAGS -D__SWITCH__ -I$PYINC -I$WORK/pyinc $(aarch64-none-elf-pkg-config --cflags $PKGS)"
D=$B/dist
rm -rf "$D" && mkdir -p "$D/lib"
python3 "$HERE/build_modules.py" "$PYGAME" "$RENPY" "$WORK/obj" 2>"$B/modules.log" || { tail -80 "$B/modules.log"; exit 1; }
cp "$WORK/obj/librenpy7.a" "$D/lib/"

# 4. Stage Python engine + boot overlay + cursor + sdkarm common/packages/fonts
STAGED=$WORK/renpy-base
"$HOSTPY" "$HERE/stage_engine.py" "$PYGAME" "$RENPY" "$SDKARM" "$STAGED" "$TOP/runtime/renpy/boot"
python3 "$TOP/runtime/renpy/make_cursor.py" "$STAGED/renpy/common/_rsl_cursor.png"
cp -R "$STAGED" "$D/renpy-base"
ls -l "$D/lib"
