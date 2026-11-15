# Crash Bandicoot XS for Android

A separate build from the PC port: `platform/android/build.sh` makes
`build/android/CrashBandicootXS.apk` (Android 5.0 and up; ARM64 phones
and tablets, and x86-64 emulators and Chromebooks).

## How it works

The game is the pc platform's core, unchanged: the same game code, PPU,
GAX2 sound and EEPROM save, built as a static Linux executable for ARM64
(Ubuntu's `aarch64-linux-gnu` cross compiler, `-static`) and x86-64. It
needs no Android libraries, so no NDK. It is in the APK as
`lib/<abi>/libcrashxs_core.so`, where Android installs native libraries
and lets an app run them.

The app (two Java classes, no libraries) starts it as a child process with
`--stream` (platform/pc/src/stream.c): the game writes each frame's
picture (240x160 RGB565) and sound (16-bit mono, 48 kHz) to stdout, and
reads the buttons from stdin, and keeps the GBA's pace itself.

- `MainActivity`: starts the core, the picture to the view, the sound to
  an AudioTrack; controllers and keyboards (the GBA's buttons by place:
  the right face button is A, the bottom one B; shoulders and triggers L
  and R; or the PC port's keys X, Z, A, S, Enter, Backspace); in the
  background it stops reading, which stops the game at its next frame.
- `GameView`: the picture, fitted with sharp pixels (a whole multiple of
  the GBA's when that loses little), and the on-screen buttons: D-pad
  (8 ways), A, B, L, R, Select, Start, several fingers at once, a buzz on
  each press. A controller in use hides them until the screen is touched.

The save is `crashbandicootxs.sav` in the app's private files; what the
core prints goes to `core.log` next to it.

## Building

Debian/Ubuntu packages: `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
binutils-aarch64-linux-gnu libc6-dev-arm64-cross openjdk-17-jdk-headless
android-sdk-platform-23 android-sdk-build-tools dalvik-exchange apksigner
zipalign`. No Gradle, no SDK manager. The APK is signed with
`crashxs.keystore` (here, so that every build can update the last one
installed without losing its save).

## Testing without a phone

`qemu-aarch64-static build/android-arm64/libcrashxs_core.so --headless
--keys platform/pc/tests/boot.keys --frames 6000` gives the PC build's
checkpoint hashes; `--stream` can be read by any program that speaks the
messages above.
