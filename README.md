# N. Larged XS

A native PC and Android port of **Crash Bandicoot XS** (*Crash Bandicoot: The Huge
Adventure*, Game Boy Advance, 2002), built from the
[Crash Bandicoot XS decompilation](https://github.com/Almamu/CrashBandicootXS-decomp).
It isn't an emulator: it's the game's own code compiled for Windows, Linux
and Android, with a modern front end on top.

> **You need your own copy of the game.** This repository and its releases
> contain no part of the ROM. The launcher builds the game from a ROM you
> supply.

## Getting started (Windows)

1. Download `NLargedXSLauncher.exe` from the
   [Releases](../../releases) page and put it in a folder of its own,
   somewhere you can write to (not Program Files).
2. Open it, choose **Import ROM** and pick your copy of the game (or drop
   the ROM onto the window).
3. Choose **Launch Game**.

The launcher writes **Crash Bandicoot XS.exe** next to itself. That exe is
the whole game: you can start it directly from now on and delete the
launcher if you like. Settings and saves live in the same folder.

**The ROM it needs:** *Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl)*,
SHA-1 `bdd061e1b5187c0528928ec0ddb6886b1de16970`. Any other version
(including the North American *Huge Adventure*) is refused and nothing is
written.

Windows may show a SmartScreen warning the first time because the launcher
isn't code-signed; choose **More info → Run anyway**. It also runs under
Wine and Proton (Linux, Steam Deck).

The launcher also has a command line, e.g.
`NLargedXSLauncher -i "Crash XS.gba" -l -- --fullscreen`. See
[platform/pc/LAUNCHER.md](platform/pc/LAUNCHER.md) for all the options.

## Android

Download `NLargedXS-<version>.apk` from the [Releases](../../releases) page
and install it. On first launch, tap **Import ROM** and pick the same
European ROM; the app keeps it in its own storage, so this happens once.
In levels the GBA's buttons are on the screen; menus and the warp room are
tapped directly, and the phone's back button is B (or pause in a level).
The cog at the top opens the settings, with phone options: touch button
size, opacity and haptics, large text, 60 or 120 frames a second. Like the
launcher, the APK contains no part of the game. See
[platform/android/README.md](platform/android/README.md).

## Features

Press **~** (or the controller's Guide button / right stick click) in game
to open the settings menu, which is drawn in the game's own style. It
works with keyboard, controller or mouse.

- **Display:** windowed or borderless fullscreen; integer scaling;
  themed borders around the picture; frame rates of 60, 90, 120, 144 and
  240 Hz with frame interpolation.
- **Graphics:** pixel-perfect, smooth, Scale2x/3x and xBRZ "cartoon"
  filters; scanlines, LCD grid, GBA screen colours and LCD ghosting.
- **Controls:** rebindable keyboard and controller (two bindings per
  action), rumble, mouse support in menus and the warp room.
- **Button prompts that match your pad:** the game's A/B/L/R/Start
  icons are redrawn for Xbox, PlayStation, Nintendo or your keyboard keys.
- **Interface:** an optional HUD and stats panel in the borders, FPS
  counter, speedrun timer and a pixel-art input display.
- **Gameplay:** auto save, look up/down (hold Up or Down while standing still to peek), and
  skip intro.
  Optional slide jump grace: as in Crash 2 and 3, a slide off a ledge can
  still jump for a few frames.
- **Audio:** high-quality resampling, with separate music and sound
  effect volumes.
- **Extras:** Discord Rich Presence, and **Add to Steam**, which adds the
  game to Steam as a non-Steam game with full library artwork (see
  [platform/pc/STEAM.md](platform/pc/STEAM.md)).
- **Portable** by default: everything stays in the game's folder.

**Default keyboard controls:** arrows = D-pad, X = A (jump), Z = B
(spin), A / S = L / R, Enter = Start, Backspace = Select, F12 =
screenshot, Tab (held) = fast forward, ~ = settings.

## Building from source

The port builds on Linux and cross-compiles for Windows with MinGW-w64.
Set up the toolchain from [INSTALL.md](INSTALL.md), plus SDL2 for the
window (`libsdl2-dev` on Debian/Ubuntu). No ROM is needed to build the
game itself.

```sh
# Linux
make PLATFORM=pc                      # build/pc/crashbandicootxs_pc

# Windows (from Linux, with SDL2's MinGW development files)
make PLATFORM=pc OBJ_DIR=build/win/crashbandicootxs \
    HOST_CC=x86_64-w64-mingw32-gcc HOST_CXX=x86_64-w64-mingw32-g++ \
    HOST_NM=x86_64-w64-mingw32-nm HOST_CXXFILT=x86_64-w64-mingw32-c++filt \
    HOST_OBJCOPY=x86_64-w64-mingw32-objcopy \
    SDL2_PREFIX=path/to/SDL2-2.30.x/x86_64-w64-mingw32
```

The launcher needs the ROM at build time, because it records where each
part of the game comes from in the ROM. The resulting launcher contains
no ROM data and can be shared:

```sh
platform/pc/launcher/build.sh path/to/rom.gba path/to/SDL2-2.30.x/x86_64-w64-mingw32
# -> build/launcher/NLargedXSLauncher.exe

# Android (also needs the ROM, for the same reason)
platform/android/build.sh path/to/rom.gba
# -> build/android/NLargedXS-<version>.apk
```

The technical details of the port, including how each feature works and
how it is tested against the original, are in
[docs/platforms.md](docs/platforms.md).

## Credits

- The decompilation this is built on:
  [Almamu/CrashBandicootXS-decomp](https://github.com/Almamu/CrashBandicootXS-decomp)
  and its contributors. Its original README is kept in
  [README_DECOMP.md](README_DECOMP.md).
- [SDL2](https://www.libsdl.org/) for the window, input and audio.
- [xBRZ](https://sourceforge.net/projects/xbrz/) by Zenju for the cartoon
  filters.

## License and disclaimer

GPLv3, see [LICENSE](LICENSE).

N. Larged XS is a fan project and isn't affiliated with or endorsed by
Activision, Vicarious Visions or Naughty Dog. Crash Bandicoot is a
trademark of Activision Publishing, Inc. Please support the official
releases.
