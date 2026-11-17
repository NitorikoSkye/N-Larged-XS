/* The Steam library's art (steam.c, "add to steam"), built into the
 * executable: the capsule (600x900), the wide capsule (920x430), the
 * hero (1920x620), the logo and the icon (platform/pc/steam-art/). Each
 * is its file's bytes between a start and an end label. */

    .section .rodata
    .balign 4
    .globl gSteamArtPortrait
gSteamArtPortrait:
    .incbin "platform/pc/steam-art/grid_portrait.jpg"
    .globl gSteamArtPortraitEnd
gSteamArtPortraitEnd:

    .balign 4
    .globl gSteamArtWide
gSteamArtWide:
    .incbin "platform/pc/steam-art/grid_wide.jpg"
    .globl gSteamArtWideEnd
gSteamArtWideEnd:

    .balign 4
    .globl gSteamArtHero
gSteamArtHero:
    .incbin "platform/pc/steam-art/hero.jpg"
    .globl gSteamArtHeroEnd
gSteamArtHeroEnd:

    .balign 4
    .globl gSteamArtLogo
gSteamArtLogo:
    .incbin "platform/pc/steam-art/logo.png"
    .globl gSteamArtLogoEnd
gSteamArtLogoEnd:

    .balign 4
    .globl gSteamArtIcon
gSteamArtIcon:
    .incbin "platform/pc/steam-art/icon.png"
    .globl gSteamArtIconEnd
gSteamArtIconEnd:
