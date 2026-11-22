# N. Larged XS launcher

`NLargedXSLauncher.exe` makes the PC port, **Crash Bandicoot XS.exe**, from
your own copy of the game. The launcher holds no part of the game: you
supply the ROM, it builds the game next to itself. Nothing else needs
installing.

## For players

1. Put `NLargedXSLauncher.exe` in a folder of its own (somewhere you can
   write to: not Program Files).
2. Open it and choose **Import ROM**, then pick your ROM in the file
   window (or drop the ROM onto the launcher's window).
3. **Launch Game** starts it. You can also start `Crash Bandicoot XS.exe`
   yourself from now on: it is the whole game, and the launcher can be
   deleted.

It also makes **Crash Bandicoot XS Wide.exe**, the game built for a wider
288x160 screen. Keep it beside the other one: the game's **widescreen**
setting (Display) restarts the game in it, and starting
`Crash Bandicoot XS.exe` with the setting on starts it too.

The ROM it needs: **Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl)**, SHA-1
`bdd061e1b5187c0528928ec0ddb6886b1de16970` (8,388,608 bytes, CRC-32
`CCFD654F`), as it is or in a `.zip` (its `.gba`, or its only file;
stored or deflated). Another ROM (the North American
*Huge Adventure*, a hacked or trimmed one) is refused, and nothing is
written.

The game keeps its settings and saves next to itself (portable mode), so the
folder is the whole install. A newer launcher makes the newer game: if the
ROM is still where you imported it from, its **Update Game** does it at
once; otherwise import it again. Saves and settings stay.

Under Proton or Wine (a Steam Deck, Linux), the launcher works too: run it
there the same way.

## The command line

```
NLargedXSLauncher [options] [-- game options]
  (no options)         open the launcher's window
  -i, --import ROM     make the game from ROM (a .gba, or a .zip with it), without
                       a window
  -g, --game ROM       --import ROM, then --launch (makes the game only if it
                       isn't up to date)
      --progress       with --import: "progress <0-1> <text>" lines on stdout
  -o, --output PATH    where to write the game (default: next to the launcher)
  -l, --launch         start the game (after --import, with both) and exit
  -c, --check ROM      only check that ROM is the right one
  -s, --status         say whether the game is here and up to date
  -f, --force          make the game again even if it is up to date
  -v, --version        the launcher's version and the game it makes
  -h, --help           this
  -- ARGS              with --launch: hand ARGS to the game (e.g. -- --fullscreen)
```

Exit status: 0 done, 1 failed, 2 not the right ROM, 3 no game to launch.
For example, `NLargedXSLauncher -i "Crash XS.gba" -l -- --fullscreen` makes
the game and starts it in the whole screen.

`--game` and `--progress` are for RecompLauncher and other front ends
(platform/pc/RECOMPLAUNCHER.md): nothing changes without them. With
`--progress` the Windows launcher doesn't attach to its parent's console,
so the lines reach the pipe it was started with.

## Linux

The Linux launcher is the same program built for Linux
(`build_linux.sh`): `NLargedXSLauncher` makes **crash-bandicoot-xs**, the
Linux game, and **crash-bandicoot-xs-wide**, its widescreen build, next to
itself. Both need glibc 2.34 or newer (Ubuntu 22.04,
Debian 12, Fedora 35, SteamOS 3 and later) and SDL2 from the system
(`libSDL2-2.0.so.0`: on Debian and Ubuntu `libsdl2-2.0-0`, on Fedora
`SDL2`, on Arch `sdl2`; Steam's systems have it). Its window takes the ROM
dropped onto it; `--import` works without a window.

## How it works

The launcher carries a *package* (`tools/make_package.py`): the game's
executable written as a list of operations that rebuild it.

- **From the ROM:** every run of 32 bytes or more that is in the ROM as it
  is (the graphics, the music, the levels: 95% of the ROM is in the game
  byte for byte) is stored as its place in the ROM, not its bytes.
- **Masked:** every other byte outside the code (the ROM's data in other
  forms: the settings menu's fonts and art, the music's structures, the
  icon) is stored XORed with a key stream made from the ROM (SHA-1 of the
  ROM's SHA-1 and a block number), so without the ROM it is noise.
- **Plain:** the code section (the decompilation's and the port's compiled
  code) as it is.

Importing checks the ROM's SHA-1, replays the operations, checks the result
against the game's SHA-1, and writes it beside the launcher (whole, then
renamed into place). It takes well under a second.

The launcher's window is the PC settings menu's category page. Its fonts
and pause-screen art are the game's, so the launcher reads them out of the
built `Crash Bandicoot XS.exe` (the package lists where they are). Before
the game is built it has no ROM data to draw with, so it draws a look-alike
of its own: the borders' riveted wall and plates and its own font in the
game's colours. Its icon is its own too (an "XS" on the borders' plate).

## Building a release

The package is made from the ROM, so a release is built where the ROM is
(CI has no ROM). From the repository's root:

```
platform/pc/launcher/build.sh path/to/crashbandicootxs.gba path/to/SDL2-2.30.9/x86_64-w64-mingw32
```

That builds the Windows game, strips it, makes `build/launcher/package.bin`,
and builds `build/launcher/NLargedXSLauncher.exe` with the package in it.
Publish `NLargedXSLauncher.exe` (and this file); never the game executable
or the ROM. `make_package.py` checks that its package rebuilds the game
exactly; a search of the launcher for runs of the ROM (16 bytes or more)
finds only zero padding and the alphabet.

Each launcher makes one exact build of the game: build the launcher again
for every release.

The Linux launcher, on Linux with SDL2's development files:

```
platform/pc/launcher/build_linux.sh path/to/crashbandicootxs.gba
```

builds the Linux game (`build/pc/crashbandicootxs_pc`), strips it, makes
its package (`make_package.py` reads an ELF's sections as it reads a PE's)
and builds `build/launcher-linux/NLargedXSLauncher`. Then

```
platform/pc/launcher/release.sh
```

puts each launcher in the release's archive with `recomplauncher.json`
beside it (`build/release/N-Larged-XS-vX.Y.Z-Windows-x64.zip` and
`-Linux-x64.tar.gz`), its setup step's version set to the release's.

One thing in the source tree is not the decompilation's: the Steam pictures
(`platform/pc/steam-art/`, the game's box art). They are built into the
game and masked in the package like the rest, but they are files in the
repository itself, so think about whether to publish them.
