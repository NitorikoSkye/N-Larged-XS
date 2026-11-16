/* The save menu without "load link game" (docs/platforms.md, "The link
 * cable"): the PC has no second console to load a game from, so the main
 * page shows four options, not five.
 *
 * The link wraps SaveMenu::DrawMain (platform.mk, HOST_WRAPS; Draw, in
 * save_menu_draw.cpp, calls it each frame of the main page, after
 * Input). MainInput, in DrawMain's own object, can't be wrapped, so its
 * cursor is mended here before the page is drawn: the link game's row
 * (cursor 1) is skipped in the direction the cursor came from, so A can
 * never pick it. The rows are drawn as DrawMain draws them, less that one,
 * moved down half a row to stay centred. HOST_LINK_GAME=1 keeps the
 * game's own page (the link test, platform/pc/tests/link.keys, compares
 * it with mGBA's). */

#include "game/ui/save_menu.hpp"

extern "C" {
#include "core.h"
#include "engine/save/save.h"
#include "engine/sys/system.h"
#include "engine/text/text.h"
#include "globals.h"
}

using Game::Ui::SaveMenu;

enum {
    ROW_LINK = 1, /* gSaveMenuOptions' "load link game" */
};

extern "C" {

#include <stdlib.h>

void __real__ZN4Game2Ui8SaveMenu8DrawMainEv(SaveMenu *self);

void __wrap__ZN4Game2Ui8SaveMenu8DrawMainEv(SaveMenu *self)
{
    static SaveMenu *sMenu;
    static s32 sLast;
    screen_y_t y = DISPLAY_ART_Y + 0x64 + 5;
    struct byte_arg flag;
    s32 i;

    if (getenv("HOST_LINK_GAME") != NULL) {
        __real__ZN4Game2Ui8SaveMenu8DrawMainEv(self);
        return;
    }
    if (self != sMenu) {
        sMenu = self;
        sLast = 0;
    }
    if (self->cursor == ROW_LINK) {
        /* Down from the first row goes on to the third; up from below
         * goes on to the first. */
        self->cursor = sLast == ROW_LINK + 1 ? ROW_LINK - 1 : ROW_LINK + 1;
    }
    sLast = self->cursor;

    for (i = 0; i < (s32)ARRAY_COUNT(gSaveMenuOptions); i++) {
        const u8 *label;

        if (i == ROW_LINK) {
            continue;
        }
        gSmallFont->SetPalette(i == self->cursor ? self->GetBlinkPalette() : 0);
        label = GetUiText(gSaveMenuOptions[i]);
        gSmallFont->SetPos((DISPLAY_WIDTH - gSmallFont->MeasureText(label)) >> 1, y);
        gSmallFont->DrawText(label);
        y += 0xa;
    }
    flag.v = 0;
    self->DrawSlotStats(DISPLAY_ART_X + 0x5a, DISPLAY_ART_Y + 0x21, 0, flag);
}
}
