/* The HUD setting (docs/platforms.md, "HUD"; Interface's HUD): the game's
 * HUD shown, hidden, or moved to the left border (borders.c).
 *
 * The link wraps Hud::Update (platform.mk, HOST_WRAPS; play_room.cpp and
 * actor_category_init.cpp call it each frame), which both counts (lives,
 * wumpa, crates, the time trial's clock) and draws the HUD's sprites.
 * It always runs; hidden, the sprites it put in the OAM buffer and the
 * sprite queue are taken back out after it (their counts put back), so
 * the game sees the same HUD and nothing else changes. Its numbers go to
 * gHostHud for the left border. A boss fight's HUD and the jetpack's
 * percentages are kept in the picture with "borders" (they aren't in
 * the border): only "off" hides them. */

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

    /* "borders": only while the left border shows it (themed borders,
     * room beside the picture); otherwise it stays in the picture. */
    if (mode == HOST_HUD_OFF || (mode == HOST_HUD_BORDERS && gHostHudInBorder && !boss && !self->icon_flag)) {
        if (gOamBuffer != NULL && gOamBuffer->count > oam) {
            gOamBuffer->count = oam;
        }
        if (sSpriteOamQueueCount > queued) {
            sSpriteOamQueueCount = queued;
        }
    }
}

} /* extern "C" */
