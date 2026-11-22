#!/bin/sh
# Builds NLargedXSLauncher.exe (platform/pc/LAUNCHER.md): the Windows game
# and the widescreen one (HOST_WIDE=288, docs/platforms.md "Widescreen"),
# their packages made from your ROM (make_package.py), then the launcher
# with both packages built in. Run from the repository's root:
#
#   platform/pc/launcher/build.sh ROM SDL2_MINGW_DIR
#
# ROM is your Crash Bandicoot XS (Europe) ROM, SDL2_MINGW_DIR SDL2's MinGW
# development files (SDL2-2.30.9/x86_64-w64-mingw32). The launcher holds no
# ROM data and can be published; the game it makes can't.
set -e
ROM=${1:?usage: build.sh ROM SDL2_MINGW_DIR}
SDL=${2:?usage: build.sh ROM SDL2_MINGW_DIR}
CROSS=${CROSS:-x86_64-w64-mingw32-}
OUT=build/launcher
VERSION=$(sed -n 's/^#define HOST_PORT_VERSION "\(.*\)"/\1/p' platform/pc/src/host_platform.h)
VERSION_NUM=$(echo "${VERSION#v}" | awk -F. '{ printf "%d,%d", $1, $2 }')
JOBS=$(nproc 2>/dev/null || echo 4)

mkdir -p "$OUT"
make -j"$JOBS" PLATFORM=pc OBJ_DIR=build/win/crashbandicootxs HOST_CC=${CROSS}gcc HOST_CXX=${CROSS}g++ \
    HOST_NM=${CROSS}nm HOST_CXXFILT=${CROSS}c++filt HOST_OBJCOPY=${CROSS}objcopy SDL2_PREFIX="$SDL"
GAME=build/win/CrashBandicootXS-$VERSION.exe
${CROSS}strip -o "$OUT/game.exe" "$GAME"
python3 platform/pc/tools/make_package.py --rom "$ROM" --exe "$OUT/game.exe" --symbols "$GAME" \
    --nm ${CROSS}nm --version "$VERSION" -o "$OUT/package.bin"
make -j"$JOBS" PLATFORM=pc HOST_WIDE=288 OBJ_DIR=build/win-wide/crashbandicootxs HOST_CC=${CROSS}gcc \
    HOST_CXX=${CROSS}g++ HOST_NM=${CROSS}nm HOST_CXXFILT=${CROSS}c++filt HOST_OBJCOPY=${CROSS}objcopy \
    SDL2_PREFIX="$SDL"
WIDE=build/win-wide/CrashBandicootXS-$VERSION.exe
${CROSS}strip -o "$OUT/game-wide.exe" "$WIDE"
python3 platform/pc/tools/make_package.py --rom "$ROM" --exe "$OUT/game-wide.exe" --symbols "$WIDE" \
    --nm ${CROSS}nm --version "$VERSION" -o "$OUT/package-wide.bin"
${CROSS}windres -I platform/pc/launcher -D "LAUNCHER_VERSION_STR=\\\"$VERSION\\\"" \
    -D "LAUNCHER_VERSION_NUM=$VERSION_NUM" -o "$OUT/launcher_res.o" platform/pc/launcher/launcher.rc
${CROSS}gcc -O2 -Wall -Wno-format-truncation -I platform/pc/include -I engine/include -iquote platform/pc/src \
    -I"$SDL/include/SDL2" -I"$SDL/include" -DPACKAGE_BIN="\"$OUT/package.bin\"" \
    -DPACKAGE_WIDE_BIN="\"$OUT/package-wide.bin\"" -o "$OUT/NLargedXSLauncher.exe" platform/pc/launcher/launcher.c platform/pc/launcher/zip_rom.c \
    platform/pc/launcher/launcher_package.S \
    platform/pc/src/sha1.c "$OUT/launcher_res.o" \
    -static -static-libgcc -mwindows "$SDL/lib/libSDL2.a" -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
    -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lcomdlg32
${CROSS}strip "$OUT/NLargedXSLauncher.exe"
echo "$OUT/NLargedXSLauncher.exe ($VERSION)"
