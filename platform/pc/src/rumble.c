/* Controller rumble (docs/platforms.md, "Rumble"; Controls' Rumble): the
 * controllers buzz with what happens in the game.
 *
 * The game says what happens with its sound effects: the link wraps
 * AudioContext::PlaySfx (platform.mk, HOST_WRAPS; it is called from all
 * over the game, every vehicle and boss included), and a table gives the
 * effects worth feeling a strength for each motor and a length. The
 * effect's own volume (its distance from Crash, for a crusher or a
 * yeti's stomp) scales it, and only effects in a level count (the
 * company logos and the cutscenes use some of them too). Every open
 * controller that can rumble does (SDL_GameControllerRumble); the next
 * one replaces the last unless it's weaker. On Android (HOST_ANDROID) the
 * same table buzzes the phone, or the controller in use (the app does
 * that, from stream.c's message 9). */

#include <stddef.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#include "constants/sfx.h"

extern s32 gHostPlayingLevel;

void __real__ZN6Engine5Audio12AudioContext7PlaySfxEjj(void *self, u32 id, u32 volume);

struct Buzz {
    u8 id;
    u8 low;  /* the heavy motor, 0-255 */
    u8 high; /* the light one */
    u16 ms;
};

static const struct Buzz sBuzz[] = {
    { SFX_PLAYER_HURT, 230, 200, 380 },
    { SFX_UNKNOWN_2E, 160, 120, 250 }, /* dying */
    { SFX_AKU_AKU_LOSE, 170, 150, 250 },
    { SFX_EXPLOSION, 255, 220, 380 },
    { SFX_BOSS_HIT, 230, 180, 320 },
    { SFX_AIRSHIP_HIT, 150, 150, 200 },
    { SFX_HOVERCRAFT_PART_HIT, 150, 150, 200 },
    { SFX_JETPACK_PLANE_DOWN, 130, 120, 220 },
    { SFX_JETPACK_PLAYER_DOWN, 230, 200, 400 },
    { SFX_UNKNOWN_42, 170, 150, 250 }, /* the jetpack hit */
    { SFX_YETI_CATCH, 230, 200, 450 },
    { SFX_YETI_STOMP_1, 120, 40, 120 },
    { SFX_YETI_STOMP_2, 120, 40, 120 },
    { SFX_TINY_LAND, 150, 60, 200 },
    { SFX_CRUSHER_SLAM, 140, 50, 150 },
    { SFX_ELECTRIC_SHOCK, 120, 220, 300 },
    { SFX_BODY_SLAM_LAND, 190, 90, 160 },
    { SFX_CRATE_BREAK, 60, 110, 80 },
    { SFX_ENEMY_KNOCKED_AWAY, 50, 90, 70 },
    { SFX_BOUNCE, 40, 80, 60 },
    { SFX_CRYSTAL, 90, 140, 220 },
    { SFX_GEM, 90, 140, 220 },
    { SFX_EXTRA_LIFE, 40, 90, 90 },
};

#ifdef HOST_SDL

#include <SDL.h>

static Uint32 sUntil;
static u32 sStrength; /* of the one buzzing */

static void Buzz(const struct Buzz *b, u32 volume)
{
    u32 scale = gHostSettings.rumble == 1 ? 128 : 256; /* light, strong */
    u32 low = b->low * scale / 256 * (volume > 0x100 ? 0x100 : volume) / 0x100;
    u32 high = b->high * scale / 256 * (volume > 0x100 ? 0x100 : volume) / 0x100;
    Uint32 now = SDL_GetTicks();
    int i;

    if (low + high < 8) {
        return;
    }
    if ((Sint32)(sUntil - now) > 0 && low + high < sStrength) {
        return; /* a stronger one is still buzzing */
    }
    sUntil = now + b->ms;
    sStrength = low + high;
    /* HOST_RUMBLE_LOG=1: each buzz on stderr, for testing without a pad */
    if (SDL_getenv("HOST_RUMBLE_LOG") != NULL) {
        SDL_Log("rumble: sfx 0x%02x low %u high %u for %u ms", b->id, low, high, b->ms);
    }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController *pad;

        if (!SDL_IsGameController(i)) {
            continue;
        }
        pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (pad != NULL) {
            SDL_GameControllerRumble(pad, (Uint16)(low * 257), (Uint16)(high * 257), b->ms);
        }
    }
}

static void Rumble(u32 id, u32 volume)
{
    size_t i;

    if (gHostSettings.rumble == 0 || gHostOptions.headless || gHostPlayingLevel < 0) {
        return;
    }
    for (i = 0; i < sizeof(sBuzz) / sizeof(sBuzz[0]); i++) {
        if (sBuzz[i].id == id) {
            Buzz(&sBuzz[i], volume);
            return;
        }
    }
}

#elif defined(HOST_ANDROID)

/* The phone (Touch's vibration, the same setting): its one motor, as
 * strong as the controller's stronger one would be; the app vibrates the
 * controller in use instead when it can (stream.c's message 9). */
void HostStreamRumble(u32 strength, u32 ms); /* stream.c */
char *getenv(const char *name);

static u32 sUntilFrame;
static u32 sStrength; /* of the one buzzing */

static void Rumble(u32 id, u32 volume)
{
    size_t i;

    /* HOST_RUMBLE_LOG=1: headless runs log what they'd send (the tests) */
    int log = getenv("HOST_RUMBLE_LOG") != NULL;

    if ((gHostSettings.rumble == 0 && !log) || (gHostOptions.headless && !log) ||
        gHostPlayingLevel < 0) {
        return;
    }
    for (i = 0; i < sizeof(sBuzz) / sizeof(sBuzz[0]); i++) {
        if (sBuzz[i].id == id) {
            const struct Buzz *b = &sBuzz[i];
            u32 scale = gHostSettings.rumble == 1 ? 128 : 256; /* light, strong (tests: strong) */
            u32 top = b->low > b->high ? b->low : b->high;
            u32 strength = top * scale / 256 * (volume > 0x100 ? 0x100 : volume) / 0x100;
            u32 now = HostFrame();

            if (strength < 8) {
                return;
            }
            if ((s32)(sUntilFrame - now) > 0 && strength < sStrength) {
                return; /* a stronger one is still buzzing */
            }
            sUntilFrame = now + b->ms * 60 / 1000;
            sStrength = strength;
            if (log) {
                HostLogf("rumble %u: sfx 0x%02x strength %u for %u ms\n", now, b->id, strength,
                         b->ms);
            }
            HostStreamRumble(strength, b->ms);
            return;
        }
    }
}

#else

static void Rumble(u32 id, u32 volume)
{
    (void)id;
    (void)volume;
}

#endif

void __wrap__ZN6Engine5Audio12AudioContext7PlaySfxEjj(void *self, u32 id, u32 volume)
{
    __real__ZN6Engine5Audio12AudioContext7PlaySfxEjj(self, id, volume);
    Rumble(id, volume);
}
