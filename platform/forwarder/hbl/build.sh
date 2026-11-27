#!/bin/bash
# Build the embedded Horizon forwarder ExeFS (main + main.npdm) into build/forwarder/hbl_embed.h.
set -euo pipefail

TOP=$(pwd)
HERE=$TOP/platform/forwarder/hbl
OUT=$TOP/build/forwarder
EXEFS=$OUT/exefs
HDR=$OUT/hbl_embed.h

# Rebuild only when inputs are newer than the generated header.
if [ -f "$HDR" ] \
   && [ "$HDR" -nt "$HERE/main.c" ] \
   && [ "$HDR" -nt "$HERE/trampoline.s" ] \
   && [ "$HDR" -nt "$HERE/hbl.json" ] \
   && [ "$HDR" -nt "$HERE/build.sh" ]; then
    exit 0
fi

source "$DEVKITPRO/switchvars.sh"
mkdir -p "$EXEFS"

ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
APP_VER=$(sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' "$TOP/Makefile" | tr -d '\r' | head -n 1)
APP_VER=${APP_VER:-0.1.0}

aarch64-none-elf-gcc -g -O2 -ffunction-sections -fdata-sections $ARCH \
    -D__SWITCH__ -DVERSION=\"${APP_VER}\" -I"$DEVKITPRO/libnx/include" \
    -c "$HERE/main.c" -o "$OUT/main.o"

aarch64-none-elf-gcc -g $ARCH -x assembler-with-cpp \
    -c "$HERE/trampoline.s" -o "$OUT/trampoline.o"

aarch64-none-elf-gcc -specs="$DEVKITPRO/libnx/switch.specs" -g $ARCH \
    -Wl,-wrap,exit \
    "$OUT/main.o" "$OUT/trampoline.o" \
    -L"$DEVKITPRO/libnx/lib" -lnx \
    -o "$OUT/hbl.elf"

"$DEVKITPRO/tools/bin/elf2nso" "$OUT/hbl.elf" "$EXEFS/main"
"$DEVKITPRO/tools/bin/npdmtool" "$HERE/hbl.json" "$EXEFS/main.npdm"

python3 - "$EXEFS/main" "$EXEFS/main.npdm" "$HDR" <<'PY'
import sys

def emit(f, name, data):
    f.write(f"static const unsigned int {name}_size = {len(data)}u;\n")
    f.write(f"static const unsigned char {name}_data[] = {{\n")
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i+16])
        f.write(f"    {chunk},\n")
    f.write("};\n\n")

main_bytes = open(sys.argv[1], "rb").read()
npdm_bytes = open(sys.argv[2], "rb").read()
with open(sys.argv[3], "w", encoding="utf-8", newline="\n") as f:
    f.write("#ifndef RSL_HBL_EMBED_H\n#define RSL_HBL_EMBED_H\n\n")
    emit(f, "hbl_main", main_bytes)
    emit(f, "hbl_npdm", npdm_bytes)
    f.write("#endif\n")
PY

ls -lh "$EXEFS/main" "$EXEFS/main.npdm" "$HDR"
