/* The mouse in the game's own menus (docs/platforms.md, "Mouse"): the
 * title screen, the language select, the save menu, the pause menu and
 * the warp room (the level select). sdl.c hands the mouse over in the
 * game screen's pixels (gHostMouse) and asks HostMouseGameKeys, once a
 * frame as it reads the keys, for the buttons to add.
 *
 * The game only knows the GBA's buttons, so the mouse plays through them
 * and what the game does with them stays the game's (its sounds, its
 * saves, auto save, the timer). Over a menu's row, moving the mouse puts
 * the menu's cursor there (with the game's own "move" sound), as the
 * D-pad would have; a left click is then an A press on it, a right click
 * a B press (START in the pause menu, which B doesn't leave). In the warp
 * room a click on a level's pad holds Left or Right until the cursor gets
 * there (the game walks it pad by pad while one is held), and a click on
 * the pad it is on is A; the world arrows and the wheel are Up and Down.
 * Gameplay ignores the mouse.
 *
 * The screen up now: the save menu while SaveMenu::Run runs
 * (autosave_hooks.cpp), the pause menu while PauseMenu::Draw is called
 * (wrapped here, for the menu's `this`), the language select while
 * gLanguageSelect is, the title screen while TitleScreen::Run runs
 * (autosave_hooks.cpp), the warp room while LevelSelect::Run does
 * (level_pick.c). */

#include "engine/text/font.hpp"
#include "game/ui/frontend.hpp"
#include "game/ui/menus.hpp"
#include "game/ui/save_menu.hpp"
#include "game/ui/level_select.hpp"
#include "engine/audio/audio.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "screen.h"
#include "engine/save/save.h"
#include "host_platform.h"
#include "host_ui.h"
}

using Game::Ui::LanguageSelect;
using Game::Ui::LevelSelect;
using Game::Ui::PauseMenu;
using Game::Ui::SaveMenu;
using Game::Ui::TitleScreen;

/* LevelSelect::instance and SaveMenu::instance, private to their classes:
 * by their symbols. */
extern LevelSelect *sLevelSelectInstance __asm__("_ZN4Game2Ui11LevelSelect8instanceE");
extern SaveMenu *sSaveMenuInstance __asm__("_ZN4Game2Ui8SaveMenu8instanceE");

extern "C" {

struct HostMouse gHostMouse;

char *getenv(const char *name); /* <stdlib.h> clashes with the game's rand */

/* Set by the wrappers around each screen's Run (autosave_hooks.cpp,
 * level_pick.c). */
TitleScreen *gHostTitleScreen;
int gHostInSaveMenu;
extern int gHostInWarpRoom; /* level_pick.c */

static PauseMenu *sPause;
static u32 sPauseFrame;

void __real__ZN4Game2Ui9PauseMenu4DrawEv(PauseMenu *self);

void __wrap__ZN4Game2Ui9PauseMenu4DrawEv(PauseMenu *self)
{
    sPause = self;
    sPauseFrame = HostFrame();
    __real__ZN4Game2Ui9PauseMenu4DrawEv(self);
}

/* The continue prompt (game over's "continue?"): its cursor, drawn as
 * ">" (gContinuePromptCursorText) by Font::DrawText beside "yes" or "no",
 * says which is selected (the prompt's instance is out of reach: Run
 * makes it, all in one object). */
static u32 sContinueFrame;
static int sContinueSel;
static int sContinueTarget = -1;

void __real__ZN6Engine4Text4Font8DrawTextEPKh(Engine::Text::Font *self, const u8 *text);

void __wrap__ZN6Engine4Text4Font8DrawTextEPKh(Engine::Text::Font *self, const u8 *text)
{
    if (text == gContinuePromptCursorText) {
        sContinueFrame = HostFrame();
        sContinueSel = self->posY >= DISPLAY_ART_Y + 0x91;
    }
    __real__ZN6Engine4Text4Font8DrawTextEPKh(self, text);
}

static int ContinueUp(void)
{
    return sContinueFrame != 0 && HostFrame() - sContinueFrame <= 2;
}

/* A click on "yes" or "no": the D-pad to it, then A. */
static u16 ContinueKeys(int left)
{
    if (left) {
        int x = gHostMouse.x - DISPLAY_ART_X;
        int y = gHostMouse.y - DISPLAY_ART_Y;

        if (x >= 0x60 && x < 0xC8 && y >= 0x83 && y < 0x9C) {
            sContinueTarget = y >= 0x8F;
        }
    }
    if (sContinueTarget < 0) {
        return 0;
    }
    if (sContinueTarget != sContinueSel) {
        return sContinueTarget ? DPAD_DOWN : DPAD_UP;
    }
    sContinueTarget = -1;
    return A_BUTTON;
}

/* The row of a list under the mouse: rows `pitch` apart from `top` (each
 * `height` high), across x0..x1; -1 if none. */
static int Row(int top, int pitch, int height, int count, int x0, int x1)
{
    int k;

    if (gHostMouse.x < x0 || gHostMouse.x >= x1 || gHostMouse.y < top - 2) {
        return -1;
    }
    k = (gHostMouse.y - (top - 2)) / pitch;
    if (k >= count || gHostMouse.y - (top - 2) - k * pitch >= height + 3) {
        return -1;
    }
    return k;
}

/* Puts a menu's cursor on `row` when the mouse moved onto it. */
static void Hover(s32 *cursor, int row)
{
    if (row >= 0 && gHostMouse.moved && *cursor != row) {
        *cursor = row;
        gAudioContext->PlaySfx(SFX_MENU_MOVE, 0x100);
    }
}

/* The save menu's main page: its rows (save_menu_hook.cpp draws four,
 * without "load link game"; the game's five with HOST_LINK_GAME). */
static int SaveMainRow(void)
{
    if (getenv("HOST_LINK_GAME") != NULL) {
        return Row(DISPLAY_ART_Y + 0x64, 10, 9, 5, DISPLAY_ART_X + 40, DISPLAY_ART_X + 200);
    }
    {
        static const int options[4] = { 0, 2, 3, 4 };
        int k = Row(DISPLAY_ART_Y + 0x64 + 5, 10, 9, 4, DISPLAY_ART_X + 40, DISPLAY_ART_X + 200);

        return k < 0 ? -1 : options[k];
    }
}

/* The save menu's slot pages: the four slots (0 and 1 the left column,
 * top and bottom; 2 and 3 the right), and "cancel" (4) under them. */
static int SaveSlot(void)
{
    int x = gHostMouse.x - DISPLAY_ART_X;
    int y = gHostMouse.y - DISPLAY_ART_Y;

    if (y >= 0x82 && y < 0x92 && x >= 0x50 && x < 0xa0) {
        return 4;
    }
    if (y < 0x18 || y >= 0x80 || x < 0x10 || x >= 0xe0) {
        return -1;
    }
    return (x >= 0x80 ? 2 : 0) + (y >= 0x4c ? 1 : 0);
}

static u16 SaveMenuKeys(SaveMenu *m, int left, int right)
{
    int row = -1;

    switch (m->state) {
    case SAVE_MENU_STATE_MAIN:
        row = SaveMainRow();
        Hover(&m->cursor, row);
        break;
    case SAVE_MENU_STATE_LOAD:
    case SAVE_MENU_STATE_LOAD_LINK:
    case SAVE_MENU_STATE_SAVE:
    case SAVE_MENU_STATE_DELETE:
        row = SaveSlot();
        Hover(&m->cursor, row);
        break;
    case SAVE_MENU_STATE_CONFIRM_DELETE:
    case SAVE_MENU_STATE_CONFIRM_OVERWRITE:
        /* "yes" at 0x87, "no" at 0x91, from x 0xb0 */
        row = Row(DISPLAY_ART_Y + 0x87, 10, 9, 2, DISPLAY_ART_X + 0xa0, DISPLAY_ART_X + 0xf0);
        Hover(&m->cursor, row);
        break;
    case SAVE_MENU_STATE_MESSAGE:
        row = 0; /* a click goes on */
        break;
    default:
        break;
    }
    if (right) {
        return B_BUTTON;
    }
    return left && row >= 0 ? A_BUTTON : 0;
}

/* The pause menu: rows from 0x4a, `rowSpacing` apart, centred on x 0x32.
 * On the music and sound rows a click's left or right half turns the
 * volume down or up, as Left and Right do. */
static u16 PauseKeys(PauseMenu *p, int left, int right, int wheel)
{
    int row = Row(DISPLAY_ART_Y + 0x4a, p->rowSpacing, 9, p->rowCount, DISPLAY_ART_X, DISPLAY_ART_X + 0x64);
    int volume = row >= 0 && (p->rows[row].type == PAUSE_ROW_TYPE_MUSIC || p->rows[row].type == PAUSE_ROW_TYPE_SOUND);

    if (row >= 0 && gHostMouse.moved && p->cursor != row) {
        p->cursor = row;
        p->flashTimer = 0x1e;
        gAudioContext->PlaySfx(SFX_MENU_MOVE, 0x100);
    }
    if (right) {
        return START_BUTTON; /* B doesn't leave the pause menu; Start resumes */
    }
    if (volume && wheel != 0) {
        return wheel > 0 ? DPAD_RIGHT : DPAD_LEFT;
    }
    if (left && row >= 0) {
        if (volume) {
            return gHostMouse.x < DISPLAY_ART_X + 0x32 ? DPAD_LEFT : DPAD_RIGHT;
        }
        return A_BUTTON;
    }
    return 0;
}

/* The warp room: the pad nearest the mouse within reach, or -1. */
static int WarpPad(LevelSelect *ls)
{
    int best = -1;
    int bestD = 22 * 22;
    int i;

    for (i = 0; i <= ls->lastIndex && i < LEVEL_SELECT_ENTRIES; i++) {
        int dx = gHostMouse.x - ls->positions[i].x;
        int dy = (gHostMouse.y - ls->positions[i].y) * 3 / 2;
        int d = dx * dx + dy * dy;

        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

static u16 WarpRoomKeys(int left, int wheel)
{
    static int sTarget = -1; /* the pad clicked */
    static int sEnter;       /* A once the picture is up */
    static int sTapped;      /* a step was pressed last frame */
    LevelSelect *ls = sLevelSelectInstance;
    int x = gHostMouse.x - DISPLAY_ART_X;
    int y = gHostMouse.y - DISPLAY_ART_Y;
    int ready;

    if (ls == NULL || ls->positions == NULL || ls->index < 0 || ls->index >= LEVEL_SELECT_ENTRIES) {
        sTarget = -1;
        sEnter = 0;
        return 0;
    }
    /* the cursor stopped on its pad, selected (the game's Draw then puts
     * the level's picture up) */
    ready = ls->bg1->IsSettled() && ls->panel->HasArrived() && ls->items[ls->index]->IsSelected();
    if (wheel != 0) {
        sTarget = -1;
        sEnter = 0;
        return wheel > 0 ? DPAD_UP : DPAD_DOWN;
    }
    if (left) {
        int pad = WarpPad(ls);

        sEnter = 0;
        /* the world arrows, at (201, 70) and (201, 87) */
        if (x >= 191 && x < 215 && y >= 60 && y < 80) {
            sTarget = -1;
            return DPAD_UP;
        }
        if (x >= 191 && x < 215 && y >= 80 && y < 100) {
            sTarget = -1;
            return DPAD_DOWN;
        }
        if (pad >= 0 && pad == ls->index) {
            sTarget = -1;
            sEnter = 1;
        } else {
            sTarget = pad;
        }
    }
    /* walking there a pad at a time, as tapping the D-pad: each step once
     * the cursor has stopped on the last (holding the direction walks on
     * without bringing each pad's picture up, and A wants it up) */
    if (sTarget >= 0 && sTarget <= ls->lastIndex && sTarget != ls->index) {
        if (ready && !sTapped) {
            sTapped = 1;
            return sTarget < ls->index ? DPAD_LEFT : DPAD_RIGHT;
        }
        sTapped = 0;
        return 0;
    }
    sTarget = -1;
    sTapped = 0;
    if (sEnter && ready && ls->bg2->IsShown()) {
        sEnter = 0;
        return A_BUTTON;
    }
    return 0;
}

/* Once a frame, as sdl.c reads the keys: the buttons the mouse adds. */
u16 HostMouseGameKeys(void)
{
    int left = gHostMouse.left;
    int right = gHostMouse.right;
    int wheel = gHostMouse.wheel;
    u16 keys = 0;

    gHostMouse.left = 0;
    gHostMouse.right = 0;
    gHostMouse.wheel = 0;
    if (!gHostMouse.inside) {
        left = 0;
        right = 0;
        wheel = 0;
        gHostMouse.moved = 0;
    }
    if (gHostInSaveMenu && sSaveMenuInstance != NULL) {
        keys = SaveMenuKeys(sSaveMenuInstance, left, right);
    } else if (sPause != NULL && HostFrame() - sPauseFrame <= 2) {
        keys = PauseKeys(sPause, left, right, wheel);
    } else if (gLanguageSelect != NULL) {
        int row = Row(DISPLAY_ART_Y + 0x32, 10, 9, 6, DISPLAY_ART_X + 40, DISPLAY_ART_X + 200);

        Hover(&gLanguageSelect->language, row);
        keys = left && row >= 0 ? A_BUTTON : 0;
    } else if (gHostTitleScreen != NULL && gHostTitleScreen->menuShown) {
        int row = Row(DISPLAY_ART_Y + 0x80, 10, 9, 3, DISPLAY_ART_X + 40, DISPLAY_ART_X + 200);

        Hover(&gHostTitleScreen->selection, row);
        keys = left && row >= 0 ? A_BUTTON : 0;
    } else if (gHostInWarpRoom) {
        keys = WarpRoomKeys(left, wheel);
    } else if (ContinueUp()) {
        keys = ContinueKeys(left);
    }
#ifdef HOST_ANDROID
    /* The phone: a tap anywhere else that isn't playing (the intro, the
     * title's "press start", a power's dialog, the credits) is A and
     * START, the buttons those take to go on. */
    else if (left && HostTouchMode() == HOST_TOUCH_ANY) {
        keys = A_BUTTON | START_BUTTON;
    }
#endif
    gHostMouse.moved = 0;
    return keys;
}

/* What the touch screen does now (the Android app's controls): the game
 * played with the on-screen buttons, a menu of the game's that a tap
 * plays (as the mouse does), or anywhere else, where a tap goes on. The
 * HUD runs each frame of a level (hud_hook.cpp). */
int HostTouchMode(void)
{
    if ((gHostInSaveMenu && sSaveMenuInstance != NULL) || (sPause != NULL && HostFrame() - sPauseFrame <= 2) ||
        gLanguageSelect != NULL || (gHostTitleScreen != NULL && gHostTitleScreen->menuShown) || gHostInWarpRoom ||
        ContinueUp()) {
        return HOST_TOUCH_MENU;
    }
    if (gHostHud.frame != 0 && HostFrame() - gHostHud.frame <= 3) {
        return HOST_TOUCH_PLAY;
    }
    return HOST_TOUCH_ANY;
}
}
