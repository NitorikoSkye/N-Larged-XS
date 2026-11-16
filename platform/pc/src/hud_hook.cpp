/* The HUD setting (docs/platforms.md, "HUD"; Interface's HUD): the game's
 * HUD shown, hidden, or moved to the left border (borders.c).
 *
 * The link wraps Hud::Update (platform.mk, HOST_WRAPS; play_room.cpp and
 * actor_category_init.cpp call it each frame), which both counts (lives,
 * wumpa, crates, the time trial's clock) and draws the HUD's sprites.
 * It always runs; hidden, the sprites it put in the OAM buffer and the
 * sprite queue are taken back out after it (their counts put back), so
 * the game sees the same HUD and nothing else changes. Its numbers go to
 * gHostHud for the left border. "Off" takes all of its sprites out;
 * "borders" only the counters' (below): a boss fight's bar and the
 * jetpack's percentages aren't in the border and stay. */

#include "game/ui/hud.hpp"
#include "game/entities/sprite_obj.hpp"
#include "game/level/level_state.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "host_platform.h"
#include "host_ui.h"
}

using Engine::Gfx::OamBuffer;
using Game::Ui::Hud;

/* sSpriteOamQueueCount, private to the class: by its symbol. */
extern s32 sSpriteOamQueueCount __asm__("_ZN6Engine3Gfx14SpriteOamQueue5countE");

extern "C" {

extern s32 gHostPlayingLevel; /* level_pick.c */

/* The vehicle rooms (the polar bear, the jetpack: ROOM_KIND_CATEGORY) run
 * in ActorCategory::Init, not LevelProgress::PlayRoom (game_frame.cpp):
 * they are in a level too, for the HUD in the border, the borders' level
 * picture, Discord, the speedrun timer and rumble. */
s32 __real__ZN6Engine5Actor13ActorCategory4InitEi(s32 category);

s32 __wrap__ZN6Engine5Actor13ActorCategory4InitEi(s32 category)
{
    s32 result;

    gHostPlayingLevel = gLevelState->room.level;
    result = __real__ZN6Engine5Actor13ActorCategory4InitEi(category);
    gHostPlayingLevel = -1;
    return result;
}

struct HostHud gHostHud;
/* Set by borders.c while the left border shows the HUD (sdl.c clears
 * it each picture). */
int gHostHudInBorder;

void __real__ZN4Game2Ui3Hud6UpdateEv(Hud *self);

void __wrap__ZN4Game2Ui3Hud6UpdateEv(Hud *self)
{
    s32 oam = gOamBuffer != NULL ? gOamBuffer->count : 0;
    s32 queued = sSpriteOamQueueCount;
    int boss = gLevelState->GetBossIndex() != BOSS_NONE;
    int mode = gHostSettings.hud;

    __real__ZN4Game2Ui3Hud6UpdateEv(self);

    gHostHud.frame = HostFrame();
    /* (the HUD's own copies only follow while a counter is slid in) */
    gHostHud.lives = gLevelState->GetLives() > 0 ? gLevelState->GetLives() : 0;
    gHostHud.wumpa = gLevelState->GetWumpa();
    gHostHud.crates = gLevelState->GetCrateCount();
    gHostHud.crateTotal = self->crateTotal;
    gHostHud.timeTrial = gLevelState->timeTrial != 0;
    gHostHud.minutes = gLevelState->GetClockMinutes();
    gHostHud.seconds = gLevelState->GetClockSeconds();
    gHostHud.tenths = gLevelState->GetClockTenths();
    gHostHud.boss = boss;

    (void)boss;
    if (mode == HOST_HUD_OFF) {
        if (gOamBuffer != NULL && gOamBuffer->count > oam) {
            gOamBuffer->count = oam;
        }
        if (sSpriteOamQueueCount > queued) {
            sSpriteOamQueueCount = queued;
        }
    }
}

/* "borders": the counters the border shows (lives, wumpa, crates, the
 * time trial's clock: Hud::Update calls them from hud.cpp) are taken out
 * of the picture, while the left border shows them (themed borders,
 * room beside the picture); the boss's and the jetpack's bars stay. */
#define COUNTER_WRAP(name)                                                                  \
    void __real__ZN4Game2Ui3Hud##name(Hud *self);                                           \
    void __wrap__ZN4Game2Ui3Hud##name(Hud *self)                                            \
    {                                                                                       \
        s32 oam = gOamBuffer != NULL ? gOamBuffer->count : 0;                               \
        s32 queued = sSpriteOamQueueCount;                                                  \
                                                                                            \
        __real__ZN4Game2Ui3Hud##name(self);                                                 \
        if (gHostSettings.hud == HOST_HUD_BORDERS && gHostHudInBorder) {                    \
            if (gOamBuffer != NULL && gOamBuffer->count > oam) {                            \
                gOamBuffer->count = oam;                                                    \
            }                                                                               \
            if (sSpriteOamQueueCount > queued) {                                            \
                sSpriteOamQueueCount = queued;                                              \
            }                                                                               \
        }                                                                                   \
    }

COUNTER_WRAP(11UpdateLivesEv)
COUNTER_WRAP(11UpdateWumpaEv)
COUNTER_WRAP(12UpdateCratesEv)
COUNTER_WRAP(11UpdateClockEv)

} /* extern "C" */
