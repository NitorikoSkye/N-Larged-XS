#!/bin/sh
# The widescreen experiment's launchers (docs/platforms.md, "Widescreen";
# the widescreen branch, not a release): the game built for a WIDTH x 160
# screen (platform.mk's HOST_WIDE, 288 by default), and a launcher for
# Windows and one for Linux that make it from your ROM, as
# "Crash Bandicoot XS Wide.exe" / crash-bandicoot-xs-wide, so it can sit
# beside the usual game (better still: in a folder of its own, since its
# settings and saves are the folder's). Run from the repository's root:
#
#   platform/pc/launcher/build_wide.sh ROM SDL2_MINGW_DIR [WIDTH]
set -e
ROM=${1:?usage: build_wide.sh ROM SDL2_MINGW_DIR [WIDTH]}
SDL=${2:?usage: build_wide.sh ROM SDL2_MINGW_DIR [WIDTH]}
WIDTH=${3:-288}
CROSS=${CROSS:-x86_64-w64-mingw32-}
OUT=build/launcher-wide
VERSION=$(sed -n 's/^#define HOST_PORT_VERSION "\(.*\)"/\1/p' platform/pc/src/host_platform.h)-w$WIDTH
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$OUT"

# Windows
make -j"$JOBS" PLATFORM=pc HOST_WIDE=$WIDTH OBJ_DIR=build/win-wide/crashbandicootxs HOST_CC=${CROSS}gcc \
    HOST_CXX=${CROSS}g++ HOST_NM=${CROSS}nm HOST_CXXFILT=${CROSS}c++filt HOST_OBJCOPY=${CROSS}objcopy \
    SDL2_PREFIX="$SDL"
GAME=$(ls -t build/win-wide/CrashBandicootXS-*.exe | head -1)
${CROSS}strip -o "$OUT/game.exe" "$GAME"
python3 platform/pc/tools/make_package.py --rom "$ROM" --exe "$OUT/game.exe" --symbols "$GAME" \
    --nm ${CROSS}nm --version "$VERSION" -o "$OUT/package-win.bin"
${CROSS}gcc -O2 -Wall -Wno-format-truncation -I platform/pc/include -I engine/include -iquote platform/pc/src \
    -I"$SDL/include/SDL2" -I"$SDL/include" -DPACKAGE_BIN="\"$OUT/package-win.bin\"" \
    -DGAME_FILE="\"Crash Bandicoot XS Wide.exe\"" \
    -o "$OUT/NLargedXSLauncher-Wide.exe" platform/pc/launcher/launcher.c platform/pc/launcher/zip_rom.c \
    platform/pc/launcher/launcher_package.S platform/pc/src/sha1.c \
    -static -static-libgcc -mwindows "$SDL/lib/libSDL2.a" -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
    -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lcomdlg32
${CROSS}strip "$OUT/NLargedXSLauncher-Wide.exe"

# Linux
make -j"$JOBS" PLATFORM=pc HOST_SDL=1 HOST_WIDE=$WIDTH OBJ_DIR=build/wide/crashbandicootxs
LGAME=build/pc/crashbandicootxs_pc_wide$WIDTH
strip -o "$OUT/game.elf" "$LGAME"
python3 platform/pc/tools/make_package.py --rom "$ROM" --exe "$OUT/game.elf" --symbols "$LGAME" \
    --nm nm --version "$VERSION" -o "$OUT/package-linux.bin"
gcc -O2 -Wall -Wno-format-truncation -I platform/pc/include -I engine/include -iquote platform/pc/src \
    $(pkg-config --cflags sdl2) -DPACKAGE_BIN="\"$OUT/package-linux.bin\"" \
    -DGAME_FILE="\"crash-bandicoot-xs-wide\"" \
    -o "$OUT/NLargedXSLauncher-Wide" platform/pc/launcher/launcher.c platform/pc/launcher/zip_rom.c \
    platform/pc/launcher/launcher_package.S platform/pc/src/sha1.c $(pkg-config --libs sdl2)
strip "$OUT/NLargedXSLauncher-Wide"
echo "$OUT/NLargedXSLauncher-Wide.exe and $OUT/NLargedXSLauncher-Wide ($VERSION)"
