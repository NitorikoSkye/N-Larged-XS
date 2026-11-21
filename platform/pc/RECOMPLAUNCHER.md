# RecompLauncher

[RecompLauncher](https://recomplauncher.com) sets fan-made PC ports up from
the player's own copy of a game, shows their settings and starts them.
`recomplauncher.json` (at the repository's root, and at the top of each
release archive beside the launcher) tells it how to do that for
N. Larged XS. Its schema: https://recomplauncher.com/schema/recomplauncher.json.

## What it says

- **The copy** (`inputs`): one file, the player's Crash Bandicoot XS
  (Europe) ROM, as a `.gba` or a `.zip` with it.
- **Setup** (`prepare`): the launcher makes the game from it, without a
  window: `NLargedXSLauncher --import <ROM> --progress` (the Windows or
  the Linux launcher, whichever the release holds). `--progress` prints
  `progress <0-1> <text>` lines as it works. When it has finished it has
  written `NLargedXSLauncher.ini` beside itself, which `done` lists, so
  RecompLauncher doesn't ask for the ROM again. `version` is the release's
  (release.sh sets it): each release's game has to be made again.
- **Start** (`start`): the game it made, `Crash Bandicoot XS.exe` on
  Windows or `crash-bandicoot-xs` on Linux. It needs no arguments and no
  ROM: it is the whole game.
- **Settings** (`config`, `settings`): `settings.ini` beside the game.
  `ensure` writes `portable=1` there when it's missing, so the game keeps
  its settings and saves in its own folder (where RecompLauncher looks)
  rather than in the user's application data. The settings shown: the
  window, frame rate and filter; skip intro, auto save and the pause
  menu's statistics; and (advanced) the update check. Each choice writes
  the same `key=value` the game's own menu does; the game reads `key =
  value` too.
- **Saves** (`saves`, `folders`): `crashbandicootxs.sav` and the play
  statistics' `crashbandicootxs.stats`, beside the game; the screenshots
  folder as an action.
- **System** (`system`): Windows 10 or newer; on Linux glibc 2.34 or newer
  and the system's SDL2.

## Releases

Each release's PC downloads are archives with the launcher and
`recomplauncher.json` at the top (`platform/pc/launcher/release.sh`):

- `N-Larged-XS-vX.Y.Z-Windows-x64.zip`: `NLargedXSLauncher.exe`
- `N-Larged-XS-vX.Y.Z-Linux-x64.tar.gz`: `NLargedXSLauncher`

The Android APK stays as it is. Neither archive holds any ROM data or the
made game.

## Checking it

In RecompLauncher: Settings, Developers, turn on the developer tools, then
"Add a build" with a release archive and read the Developer tab.
