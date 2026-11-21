/* Play statistics in the game (docs/platforms.md, "Play statistics";
 * stats.c keeps them): the crates counted, and their page on the pause
 * menu's monitor.
 *
 * The monitor shows the game's five info pages in turn, 3 seconds each
 * (PauseMenu::Animate: crystals, powers, gems, relics, the time trial);
 * a sixth, "statistics", follows the fifth: the play time, deaths and
 * crates broken of the game under way, each a label and its number, in
 * the small font, centred under the page title as the game centres its
 * own (DrawPageTitle's x 0xc2). Its title is in the game's language.
 *
 * The link wraps (platform.mk, HOST_WRAPS) Animate (called from
 * pause_menu.cpp; it moves to the page after the fifth), DrawPageTitle
 * (called from Draw, pause_menu_draw.cpp; the sixth page has no title in
 * the game's table, gPauseMenuPageTitles, so it draws its own and the
 * page with it), and LevelState's AddBrokenCrate and PressSwitchCrate
 * (the crates' files call them). Headless runs leave the page out, as
 * they leave the pause menu's settings row out (HOST_PORT_MENUS=1 puts
 * it in). */

#include "engine/text/font.hpp"
#include "game/level/level_state.hpp"
#include "game/ui/menus.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "screen.h"
#include "host_platform.h"
#include "host_ui.h"
}

using Game::Level::LevelState;
using Game::Ui::PauseMenu;

extern "C" {

int HostGameOnlyTest(void); /* mouse_hooks.cpp */
extern s32 gLanguage;

/* The game's five pages are 0-4. */
#define STATS_PAGE 5

/* In each language (as prompts.c's labels: English, French, German,
 * Spanish, Italian, Dutch), in the font's letters. */
static const char *const sTitle[6] = {
    "statistics", "statistiques", "statistik", "estadisticas", "statistiche", "statistieken",
};
static const char *const sTimeLabel[6] = {
    "play time", "temps de jeu", "spielzeit", "tiempo", "tempo", "speeltijd",
};
static const char *const sDeathsLabel[6] = {
    "deaths", "morts", "tode", "muertes", "morti", "doden",
};
static const char *const sCratesLabel[6] = {
    "crates", "caisses", "kisten", "cajas", "casse", "kisten",
};

static int Language(void)
{
    return gLanguage >= 0 && gLanguage < 6 ? gLanguage : 0;
}

void __real__ZN4Game2Ui9PauseMenu7AnimateEv(PauseMenu *self);

void __wrap__ZN4Game2Ui9PauseMenu7AnimateEv(PauseMenu *self)
{
    s32 before = self->page;

    if (HostGameOnlyTest()) {
        __real__ZN4Game2Ui9PauseMenu7AnimateEv(self);
        return;
    }
    /* On the sixth page the game's own switch animates nothing, and its
     * turn (page + 1, modulo 5) would go to the second: back to the
     * first instead. After the fifth, the sixth. */
    __real__ZN4Game2Ui9PauseMenu7AnimateEv(self);
    if (before == STATS_PAGE && self->page != STATS_PAGE) {
        self->page = 0;
    } else if (before == 4 && self->page == 0) {
        self->page = STATS_PAGE;
    }
}

static void Centred(const char *text, s32 y)
{
    const u8 *t = (const u8 *)text;
    u32 w = gSmallFont->MeasureText(t);

    gSmallFont->SetPos(DISPLAY_ART_X + 0xc2 - (w >> 1), DISPLAY_ART_Y + y);
    gSmallFont->DrawText(t);
}

/* The page: three labels from y STATS_TOP, STATS_PITCH apart, each with
 * its number under it (white, as the selected row). */
#define STATS_TOP   0x48
#define STATS_PITCH 0x18
#define STATS_VALUE 10 /* the number's line under its label */

static void DrawStatsPage(void)
{
    const struct HostPlayStats *s = HostStatsCurrent();
    const char *labels[3];
    char values[3][24];
    int lang = Language();
    int i;

    labels[0] = sTimeLabel[lang];
    labels[1] = sDeathsLabel[lang];
    labels[2] = sCratesLabel[lang];
    HostStatsTimeText(values[0], sizeof(values[0]), s->frames);
    HostStatsNumberText(values[1], sizeof(values[1]), s->deaths);
    HostStatsNumberText(values[2], sizeof(values[2]), s->crates);
    for (i = 0; i < 3; i++) {
        gSmallFont->ResetPalette();
        Centred(labels[i], STATS_TOP + STATS_PITCH * i);
        gSmallFont->SetPalette(0xf);
        Centred(values[i], STATS_TOP + STATS_PITCH * i + STATS_VALUE);
    }
    gSmallFont->ResetPalette();
}

void __real__ZN4Game2Ui9PauseMenu13DrawPageTitleEv(PauseMenu *self);

void __wrap__ZN4Game2Ui9PauseMenu13DrawPageTitleEv(PauseMenu *self)
{
    if (self->page != STATS_PAGE) {
        __real__ZN4Game2Ui9PauseMenu13DrawPageTitleEv(self);
        return;
    }
    /* the title in the palette the game leaves the font in, as its own */
    Centred(sTitle[Language()], 0x2c);
    DrawStatsPage();
}

void __real__ZN4Game5Level10LevelState14AddBrokenCrateEv(LevelState *self);
void __real__ZN4Game5Level10LevelState16PressSwitchCrateEv(LevelState *self);

void __wrap__ZN4Game5Level10LevelState14AddBrokenCrateEv(LevelState *self)
{
    HostStatsCrate(1);
    __real__ZN4Game5Level10LevelState14AddBrokenCrateEv(self);
}

/* The switch crate: the crates it breaks at once (its nitros' and
 * outlines' count, pendingSwitchCrates). */
void __wrap__ZN4Game5Level10LevelState16PressSwitchCrateEv(LevelState *self)
{
    if (!self->switchPressed && self->pendingSwitchCrates > 0) {
        HostStatsCrate((u32)self->pendingSwitchCrates);
    }
    __real__ZN4Game5Level10LevelState16PressSwitchCrateEv(self);
}

} /* extern "C" */
