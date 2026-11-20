# N. Larged XS for Android

The PC port's game on Android phones and tablets (Android 5.0 and up;
ARM64, and x86-64 for emulators and Chromebooks): `platform/android/build.sh
ROM` makes `build/android/NLargedXS-<version>.apk`. It has the same version
as the PC port.

## For players

1. Install the APK (allow installing from your browser or file manager when
   Android asks).
2. Open it, tap **Import ROM** and pick your copy of the game: **Crash
   Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl)**, SHA-1
   `bdd061e1b5187c0528928ec0ddb6886b1de16970`. Any other file is refused.
   The ROM is kept in the app's own storage, so this is done once (and an
   update of the app uses it again by itself).
3. Play.

**Touch.** In a level, the GBA's buttons are on the screen, each showing what
it does in white, drawn as cartoon brush strokes that swell and taper
(icons/make_icons.py writes icons/*.svg; build.sh renders them): the D-pad on the left, A (jump: a trail
rising, over and down) and B (spin: a tornado) on the right, L (turbo run: a
sneaker leaning into a run, speed streaming off it) and R (crouch and
body slam: an arrow curling over and down onto the ground, which it hits
hard) in the top corners, Select (show the counters) and Start (pause) at the bottom. Hold the turbo
run's button for a moment to lock it on (**Auto**, handy for time trials):
it buzzes, turns orange and says AUTO, and stays held in levels (through
pausing) until you tap it again. Everywhere else (the title screen, the save menu, the warp
room, the pause menu, game over's "continue?") they're hidden and you tap
what you want, as with the PC port's mouse: a tap on a menu row picks it, a
tap on a warp room pad walks Crash there and a tap on the pad he's on
enters it. On a screen that only waits (the intro, a power's dialog, the
credits) a tap goes on. With no controller connected, the warp room's
"start: load-save" says "tap: load-save" (in each language), and tapping
it opens the load and save menu. The phone's back button is B in the menus, pauses
in a level, and closes the settings menu. A controller works too (it hides
the touch buttons until you touch the screen again), and its Guide button
opens the settings.

**The settings** (the cog at the top middle): the PC port's settings menu,
drawn in the game's style, with the phone's own options:

- **General:** skip intro.
- **Display:** frame rate (60, or 120 on a 120 Hz screen: pictures between
  the game's own, the game at its own speed), scaling (fit, integer,
  stretch), filter (pixels or smooth), colours (the game's own, or the
  GBA's darker screen), borders beside the picture (black, ambient: its
  colours blurred, or themed: the PC port's warp room walls, with the
  level's porthole and name on the left and your progress on the right,
  both kept above the touch buttons).
- **Touch:** the buttons' size (small to huge) and opacity, whether they
  also show in the menus, and haptics (off, light, strong).
- **Interface:** text size (normal, or large: the menu's rows in the
  game's big font, four to a page), the HUD (in the game, hidden, or in
  the left border), and button prompts (the
  game's A, B, L, R and Start drawn as your controller's).
- **Audio:** quality, and master, music and sound effects volumes.
- **Gameplay:** auto save, looking up and down, and the options that change
  how Crash plays, all off by default: slide jump grace (as in Crash 2 and
  3, a slide off a ledge can still jump for a moment), jump buffer (A
  pressed just before landing still jumps), relaxed double jump (for the
  whole fall, not just near the top) and infinite lives.

## How it works

The game is the pc platform's core, unchanged: the same game code, PPU, GAX2
sound and EEPROM save, built with `HOST_ANDROID=1` as a static Linux
executable for ARM64 and x86-64. It needs no Android libraries, so no NDK.
Each ABI has two builds of it, where Android installs native libraries and
lets an app run them:

- `lib/<abi>/libcrashxs_core.so`, linked with musl (`musl-cc`: GCC with
  musl's headers, musl's static libc from zig's, `pip install ziglang`).
  musl's start-up asks the kernel for very little, and nothing an app may
  not do. The one the app starts.
- `lib/<abi>/libcrashxs_gnu.so`, linked with glibc. The app tries it when
  the first stops before its first picture.

The app (Java, no libraries: `MainActivity`, `GameView`, `GameData`)
starts it as a child process with `--stream` (platform/pc/src/stream.c,
whose comment has the messages): the core writes the pictures (240x160
RGB565; 480x320 at 120 frames a second), the sound (16-bit mono, 48 kHz),
its touch mode, its settings and toasts to stdout, and reads the buttons,
taps, back, scrolls, the screen's refresh rate and the controller's prompt
style from stdin. It keeps the GBA's pace itself; in the background the
app stops reading, which stops the game at its next frame.

The settings live in the core (`phone.c`: settings.ini in the app's files,
`CRASHXS_DIR`), and its settings menu is ui.c's with the phone's categories;
the core tells the app what it needs of them (the buttons' size, opacity
and haptics, the scaling and filter, the frame rate). The touch mode comes
from `HostTouchMode` (mouse_hooks.cpp): a level (the HUD ran this frame),
one of the game's menus (as the mouse knows them), the settings menu, or
anywhere else.

The themed borders are the PC port's (borders.c): the app says how much
screen there is beside and above the picture (messages 9 and 10), and the
core draws each bar in its own pixels at the size the window would
(`HostBordersPhone`) and sends it when it changes (message 6); the app
draws it nearest-neighbour beside the picture. On the phone the bars keep
their lower half clear for the touch buttons, and the right one has a
compact progress plate (completion, crystals, gems, relics). Ambient is
the app's: the picture drawn down to 8x5 and up across the screen,
smoothly, darkened.

The save is `crashbandicootxs.sav` in the app's private files; what the
cores print goes to `core0.log` and `core1.log` next to it. If the game
stops, the screen says how each core ended (its exit code or signal, the
log's last lines) and the phone's Android version, model and ABI.

## The ROM

The APK holds no part of the game, so it can be published. Each core is
built with `-z separate-code` (its data never shares a page with code) and
a link map. `tools/make_core_package.py` then blanks out the game's data in
it: every input section the map says is from the build's own objects in
`.rodata`, `.data` and `.data.rel.ro` (not the C library's, which runs before
the data is back, nor the code's merged string literals), and writes the
recipe that makes it back from the ROM, `assets/<abi>/<core>.pkg`: runs of 32
bytes or more that are in the ROM as their places in it (85% of the data),
the rest XORed with a key stream made from the ROM (SHA-1 of the ROM's
SHA-1 and a block number), as the PC launcher's package does. The data's
SHA-1 is the core's build id, written into the blanked core.

On import the app checks the ROM's SHA-1 and keeps it; before starting a
core it makes that core's data file (`GameData`, the same steps as
`tools/make_data_file.py`) unless one for the same build id is there. The
core reads it first thing (`rom_data.c`: a constructor that runs before any
of the game's) and writes each region back in place, making read-only pages
writable for that (they're the process's own copies then; nothing in them
runs), then read-only again. A data file for another build is refused.

What of the ROM is left in the APK: runs of a byte and zeros (padding), and
a few hundred bytes of the code's own literals (level names the Discord
status uses on PC, small number tables).

## Building

`platform/android/build.sh ROM`. Debian/Ubuntu packages:
`gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu
libc6-dev-arm64-cross openjdk-17-jdk-headless android-sdk-platform-23
android-sdk-build-tools dalvik-exchange apksigner zipalign`, and `pip
install ziglang` (musl), and `librsvg2-bin` (the buttons' icons). No Gradle, no SDK manager.

**Signing.** Android installs an update only if it's signed with the same
key as the app installed, so whoever has the key can publish updates. The
release key is therefore never in the repository: a release is built with
`NLXS_KEYSTORE=path/to/release.p12 NLXS_KEYSTORE_PASS_FILE=path/to/password
platform/android/build.sh ROM` (alias `nlxs`, or `NLXS_KEY_ALIAS`). Keep the
key and its password backed up: without them no release can update the
app already installed. Without `NLXS_KEYSTORE` the APK is signed with a
development key made in build/ and named `-dev`: for testing, and it can't
update a release or be updated by one. (Until v0.47 the APKs were signed
with a key that was in the repository; v0.48 is the first signed with the
release key, so an earlier install has to be uninstalled once.)

## Testing without a phone

- `tools/stream_test.py CORE SCRIPT OUTDIR` plays the app's part against a
  core: keys, taps, back, the settings button, the refresh rate, at given
  frames, writing pictures as PNGs and printing the touch modes, settings
  and toasts. The x86-64 core runs as it is on a Linux PC.
- The blanked cores with their data: `tools/make_data_file.py PKG ROM DAT`,
  then `CRASHXS_DATA=DAT core --headless --keys platform/pc/tests/boot.keys
  --frames 6000 --checkpoint-every 500` (ARM64 under `qemu-aarch64-static`)
  gives the PC build's checkpoint hashes.
- `HOST_UI_LARGE=1 core --ui-preview DIR`: the settings menu's pages with
  large text (and a report of any row too wide).
