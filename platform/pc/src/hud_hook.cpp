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

#include "infinity_glyph.h"

using Engine::Gfx::OamBuffer;
using Game::Ui::Hud;

/* sSpriteOamQueueCount, private to the class: by its symbol. */
extern s32 sSpriteOamQueueCount __asm__("_ZN6Engine3Gfx14SpriteOamQueue5countE");

extern "C" {

void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
extern u8 gHostPltt[];

extern s32 gHostPlayingLevel; /* level_pick.c */
extern int gHostStream;       /* stream.c */
char *getenv(const char *name);

/* HOST_TOUCH_ICONS_LOG=1: the touch buttons' icons (touch_context.cpp)
 * worked out and logged in headless runs too, for the tests. */
static int TouchIconsLog(void)
{
    static int sOn = -1;

    if (sOn < 0) {
        sOn = getenv("HOST_TOUCH_ICONS_LOG") != NULL;
    }
    return sOn;
}
/* The vehicle room running (CATEGORY_TYPE_*), or -1: touch_context.cpp. */
int gHostCategoryType = -1;

/* The vehicle rooms (the polar bear, the jetpack: ROOM_KIND_CATEGORY) run
 * in ActorCategory::Init, not LevelProgress::PlayRoom (game_frame.cpp):
 * they are in a level too, for the HUD in the border, the borders' level
 * picture, Discord, the speedrun timer and rumble. */
s32 __real__ZN6Engine5Actor13ActorCategory4InitEi(s32 category);

s32 __wrap__ZN6Engine5Actor13ActorCategory4InitEi(s32 category)
{
    s32 result;

    gHostPlayingLevel = gLevelState->room.level;
    /* the descriptors' types: categories 0-2 the polar ride, 3-5 the
     * jetpack, 6 the hovercraft fight (the jetpack too; actor_anim.h) */
    gHostCategoryType = category <= 2 ? 0 : category <= 5 ? 1 : 2;
    if (TouchIconsLog()) {
        HostLogf("category %d type %d\n", (int)category, gHostCategoryType);
    }
    result = __real__ZN6Engine5Actor13ActorCategory4InitEi(category);
    gHostPlayingLevel = -1;
    gHostCategoryType = -1;
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
    if (!gHostStream && TouchIconsLog()) {
        u8 icons[4];

        HostTouchIcons(icons, 1); /* the tests: it logs a change */
    }
    /* (the HUD's own copies only follow while a counter is slid in) */
    gHostHud.lives = gLevelState->GetLives() > 0 ? gLevelState->GetLives() : 0;
    gHostHud.infiniteLives = HostInfiniteLives();
    memcpy(gHostHud.pal, (const u8 *)gHostPltt + 0x200 + 32 * self->parts[0].palette,
           sizeof(gHostHud.pal));
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

/* Gameplay's infinite lives (gameplay_hooks.cpp): the lives count is an
 * infinity sign (infinity_glyph.h), in the digits' place and palette.
 * Hud::UpdateLives draws the icon with its digits hidden (their frames
 * and the shown count put back after, so nothing it keeps changes), then
 * the sign: one 32x16 OBJ, its tiles queued like a sprite's
 * (ObjVramCursor::Upload, from sInfinityTiles: the queue copies them at
 * the next VBlank). */
static u8 sInfinityTiles[32 * 16 / 2];

static void BuildInfinityTiles(void)
{
    static int sBuilt;
    int x;
    int y;

    if (sBuilt) {
        return;
    }
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 32; x++) {
            char ch = sInfinityLarge[y][x];
            int v = ch <= '9' ? ch - '0' : ch - 'A' + 10;
            int tile = (y / 8) * 4 + x / 8;
            u8 *b = &sInfinityTiles[tile * 32 + (y % 8) * 4 + (x % 8) / 2];

            *b |= (x & 1) ? v << 4 : v;
        }
    }
    sBuilt = 1;
}

static void DrawInfinity(Hud *self)
{
    Game::Ui::HudPart *digit = &self->parts[0];
    struct oam_attrs oam;
    s32 x = Q8_TO_INT(digit->x) - 7;
    s32 y = Q8_TO_INT(digit->y) - 7 + gHudSlideOffset;
    s32 tile;

    if (y + 16 <= 0 || gOamBuffer == NULL) {
        return;
    }
    BuildInfinityTiles();
    tile = gObjVramCursor->Upload(sInfinityTiles, sizeof(sInfinityTiles));
    if (tile < 0) {
        return;
    }
    memset(&oam, 0, sizeof(oam));
    oam.y = y;
    oam.x = x;
    oam.shape = 1; /* wide */
    oam.size = 2;  /* 32x16 */
    oam.tileNum = tile;
    oam.priority = digit->GetPriority();
    oam.palette = digit->palette;
    gOamBuffer->Add(&oam);
}

void __real__ZN4Game2Ui3Hud11UpdateLivesEv(Hud *self);

static void UpdateLives(Hud *self)
{
    s32 shown = self->shownLives;
    s32 f0 = self->parts[0].frame;
    s32 f1 = self->parts[1].frame;

    if (!HostInfiniteLives() || self->livesSlide == HUD_SLIDE_STATE_HIDDEN) {
        __real__ZN4Game2Ui3Hud11UpdateLivesEv(self);
        return;
    }
    /* the count as it is: no new digits */
    self->shownLives = gLevelState->GetLives() > 0 ? gLevelState->GetLives() : 0;
    self->parts[0].frame = -1;
    self->parts[1].frame = -1;
    __real__ZN4Game2Ui3Hud11UpdateLivesEv(self);
    self->parts[0].frame = f0;
    self->parts[1].frame = f1;
    self->shownLives = shown;
    DrawInfinity(self);
}

void __wrap__ZN4Game2Ui3Hud11UpdateLivesEv(Hud *self)
{
    s32 oam = gOamBuffer != NULL ? gOamBuffer->count : 0;
    s32 queued = sSpriteOamQueueCount;

    UpdateLives(self);
    if (gHostSettings.hud == HOST_HUD_BORDERS && gHostHudInBorder) {
        if (gOamBuffer != NULL && gOamBuffer->count > oam) {
            gOamBuffer->count = oam;
        }
        if (sSpriteOamQueueCount > queued) {
            sSpriteOamQueueCount = queued;
        }
    }
}
COUNTER_WRAP(11UpdateWumpaEv)
COUNTER_WRAP(12UpdateCratesEv)
COUNTER_WRAP(11UpdateClockEv)

} /* extern "C" */
