# Crash Bandicoot XS in Steam

The PC port runs as a non-Steam game: Steam's overlay, its controller
support (Steam Input) and Big Picture / Steam Deck all work with it.

## Adding it: the quick way

In the port's settings (~), **Extras → Add to steam**, with Steam closed
(Steam → Exit; the row is grey while Steam runs). It adds "Crash Bandicoot XS" to the library of the Steam
account that logged in last, with its capsule, wide capsule, hero, logo
and icon (built into the .exe), and says so in a toast; open Steam and
it's there. Done again from a newer version, it points the same
shortcut at the new .exe and keeps its play time, launch options and
collections. Steam's shortcuts.vdf is backed up as shortcuts.vdf.bak
first. On a Steam Deck or Linux, use the Linux build for this (under
Proton the game can't see your real Steam), or add the .exe by hand.

## Adding it by hand

1. In Steam: **Games → Add a Non-Steam Game to My Library… → Browse…**,
   pick `CrashBandicootXS-vX.XX.exe`, **Add Selected Programs**.
2. Right-click it in the library → **Properties…**:
   - rename it to `Crash Bandicoot XS`;
   - **Launch options**: `--fullscreen` starts it in the whole screen
     whatever the settings say (`--windowed` the other way). Neither
     changes `settings.ini`.
   - the icon: `steam-art/icon.png`.
3. A new version is a new file name: point the shortcut's **Target** at
   it (Properties → Shortcut), or keep a copy named
   `CrashBandicootXS.exe` for Steam.

## The library's pictures

Right-click the game → **Manage → Set custom artwork** for the library
capsule (`grid_portrait.jpg`), and on the game's page right-click
the banner → **Set custom background** (`hero.jpg`) and **Set custom
logo** (`logo.png`). The wide capsule (`grid_wide.jpg`) is what
recent-games rows show (SteamGridDB-style tools set it). The pictures
are the game's box art and logo.

## Controllers

The port reads controllers through SDL, which Steam Input feeds: any
controller Steam supports works, with Steam's own **Gamepad** layout
(the default). The port's settings menu opens with ~ on the keyboard, or
the Guide button or a click of the right stick on a controller; under
Steam the Guide button opens Steam's own menu, so use the right stick.
Its Controls page has the bindings and the rumble.

## Steam Deck

Add the Windows `.exe` as above (in Desktop mode), then in its
Properties → **Compatibility**, tick **Force the use of a specific Steam
Play compatibility tool** (Proton Experimental or a recent Proton). On
the Deck (Steam sets `SteamDeck=1`), the port starts in the whole screen
until its settings say otherwise; 60 or 90 FPS suit the Deck's screen,
and the themed borders fill its 16:10 sides.

## What a non-Steam game can't have

Steam achievements, cloud saves and Steam's own rich presence need a
Steam app id, which only games sold on Steam get. The port keeps its
saves in `%APPDATA%\CrashBandicootXS` (or next to the .exe in portable
mode) - portable mode in a synced folder gives you cloud saves of your
own - and its status goes to Discord (Extras).
