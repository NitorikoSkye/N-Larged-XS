#!/bin/sh
# Builds the Linux launcher, NLargedXSLauncher (platform/pc/LAUNCHER.md,
# "Linux"): the Linux game (an x86-64 ELF, SDL2 from the system), its
# package made from your ROM (make_package.py), then the launcher with the
# package built in. Run from the repository's root, on Linux with SDL2's
# development files (libsdl2-dev):
#
#   platform/pc/launcher/build_linux.sh ROM
#
# The launcher holds no ROM data and can be published; the game it makes
# (crash-bandicoot-xs) can't. Both need glibc 2.34 or newer and the
# system's SDL2 (libSDL2-2.0.so.0): `objdump -T` on them shows the newest
# GLIBC_ symbol version they use.
set -e
ROM=${1:?usage: build_linux.sh ROM}
OUT=build/launcher-linux
VERSION=$(sed -n 's/^#define HOST_PORT_VERSION "\(.*\)"/\1/p' platform/pc/src/host_platform.h)
JOBS=$(nproc 2>/dev/null || echo 4)

mkdir -p "$OUT"
make -j"$JOBS" PLATFORM=pc HOST_SDL=1
GAME=build/pc/crashbandicootxs_pc
strip -o "$OUT/game.elf" "$GAME"
python3 platform/pc/tools/make_package.py --rom "$ROM" --exe "$OUT/game.elf" --symbols "$GAME" \
    --nm nm --version "$VERSION" -o "$OUT/package.bin"
gcc -O2 -Wall -Wno-format-truncation -I platform/pc/include -I engine/include -iquote platform/pc/src \
    $(pkg-config --cflags sdl2) -DPACKAGE_BIN="\"$OUT/package.bin\"" \
    -o "$OUT/NLargedXSLauncher" platform/pc/launcher/launcher.c platform/pc/launcher/zip_rom.c \
    platform/pc/launcher/launcher_package.S platform/pc/src/sha1.c $(pkg-config --libs sdl2)
strip "$OUT/NLargedXSLauncher"
echo "$OUT/NLargedXSLauncher ($VERSION)"
