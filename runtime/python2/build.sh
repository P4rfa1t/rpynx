#!/bin/bash
# Build static CPython 2.7.18 for the Nintendo Switch (build/python2/dist).
# Builds inside container-local /tmp for fast I/O, keeping dl/ and dist/ in /src/build/python2.
set -euo pipefail

PYVER=2.7.18
SHA256=b62c0e7937551d0cc02b8fd5cb0f544f9405bafc9a54d3808ed4594812edef43
TOP=$(pwd)
B=$TOP/build/python2
HERE=$TOP/runtime/python2
WORK=/tmp/rpynx-py2-work
JOBS=${JOBS:-$(nproc)}
export SOURCE_DATE_EPOCH=1587340800

mkdir -p "$B/dl" "$WORK"
TARBALL=$B/dl/Python-$PYVER.tar.xz
[ -f "$TARBALL" ] || curl -fL -o "$TARBALL" "https://www.python.org/ftp/python/$PYVER/Python-$PYVER.tar.xz"
echo "$SHA256  $TARBALL" | sha256sum -c -

# 1. Host interpreter + pgen (built in /tmp, cached in /src/build/python2/host).
HOST_PREFIX=$WORK/host
HOSTPY=$B/host/bin/python2.7
HOSTPGEN=$B/host/bin/pgen
if [ ! -x "$HOSTPY" ] || [ ! -x "$HOSTPGEN" ]; then
    rm -rf "$WORK/host-src" "$HOST_PREFIX" && mkdir -p "$WORK/host-src" "$HOST_PREFIX/bin"
    tar -xf "$TARBALL" -C "$WORK/host-src"
    host() { env -i PATH=/usr/local/bin:/usr/bin:/bin HOME="${HOME:-/root}" SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" "$@"; }
    HOST_CFLAGS="-O2 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
    (cd "$WORK/host-src/Python-$PYVER" &&
        host ./configure --prefix="$HOST_PREFIX" --without-ensurepip CFLAGS="$HOST_CFLAGS" >"$B/host-configure.log" &&
        host make -j"$JOBS" >"$B/host-make.log" 2>&1 &&
        host make -j"$JOBS" Parser/pgen >>"$B/host-make.log" 2>&1 &&
        host make -j"$JOBS" install >"$B/host-install.log" 2>&1 &&
        cp Parser/pgen "$HOST_PREFIX/bin/pgen")
    rm -rf "$HOST_PREFIX/lib/python2.7/test" "$HOST_PREFIX/lib/python2.7/idlelib" "$HOST_PREFIX/lib/python2.7/lib-tk"
    find "$HOST_PREFIX" -name '*.pyo' -delete
    rm -rf "$B/host" && cp -R "$HOST_PREFIX" "$B/host"
fi
"$HOSTPY" -c 'import sys, zlib; assert sys.version.startswith("'"$PYVER"'")'

# 2. Patched source (in /tmp).
SRC=$WORK/src/Python-$PYVER
rm -rf "$WORK/src" && mkdir -p "$WORK/src"
tar -xf "$TARBALL" -C "$WORK/src"
for p in "$HERE"/patches/*.diff; do
    patch -d "$SRC" -p1 --no-backup-if-mismatch -s <"$p"
done

source "$DEVKITPRO/switchvars.sh"
export LDFLAGS="$LDFLAGS -specs=$DEVKITPRO/libnx/switch.specs"
export CONFIG_SITE=$HERE/config.site
export CFLAGS="$CFLAGS $CPPFLAGS -std=gnu11 -D__SWITCH__ -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
export PYTHON_FOR_BUILD="$HOSTPY"
export PGEN_FOR_BUILD="$HOSTPGEN"

OBJ=$WORK/obj
rm -rf "$OBJ" && mkdir -p "$OBJ/Modules"
cp "$HERE/Setup.local" "$OBJ/Modules/Setup.local"
cd "$OBJ"
"$SRC/configure" \
    --host=aarch64-none-elf --build="$("$SRC/config.guess")" \
    --prefix=/ \
    --disable-shared --disable-ipv6 --with-threads --without-ensurepip \
    >"$B/configure.log" 2>&1 || { tail -40 "$B/configure.log"; exit 1; }

cat >>pyconfig.h <<'EOF2'

/* Horizon (newlib): added by runtime/python2/build.sh */
#ifndef SSIZE_MAX
#define SSIZE_MAX __LONG_MAX__
#endif
#undef HAVE_SETGROUPS
#undef HAVE_CHROOT
#undef HAVE_FCHDIR
#undef HAVE_FDATASYNC
#undef HAVE_FORK
#undef HAVE_POLL
#undef HAVE_POLL_H
#undef HAVE_SYS_POLL_H
#undef HAVE_EXECV
#undef HAVE_KILL
#undef HAVE_KILLPG
#undef HAVE_WAITPID
#undef HAVE_FSTATVFS
EOF2

# 3. Build libpython2.7.a, headers, and compiled stdlib zip.
make -j"$JOBS" libpython2.7.a >"$B/make.log" 2>&1 || { tail -80 "$B/make.log"; exit 1; }

D=$B/dist
rm -rf "$D" && mkdir -p "$D/include/python2.7" "$D/lib"
cp libpython2.7.a "$D/lib/"
aarch64-none-elf-nm "$D/lib/libpython2.7.a" > "$WORK/syms.txt"
for sym in initzlib init_io init_struct initcPickle initmath inittime init_collections init_json init_md5 init_sha256 initunicodedata initbinascii init_socket initzipimport; do
    if ! grep -q " T ${sym}$" "$WORK/syms.txt"; then
        echo "MISSING static module symbol in libpython2.7.a: $sym"
        exit 1
    fi
done
cp -R "$SRC/Include/." "$D/include/python2.7/"
cp pyconfig.h "$D/include/python2.7/"
"$HOSTPY" "$HERE/pack_stdlib.py" "$SRC/Lib" "$D/lib/python27.zip" "$HERE/_sysconfigdata.py"
ls -l "$D/lib"
