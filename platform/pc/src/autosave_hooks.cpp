/* Auto save's view of the game's save menu, and its write (docs/platforms.md,
 * "Auto save"; the decisions are autosave.c's).
 *
 * The link wraps (platform.mk, HOST_WRAPS) the calls from other files:
 * - TitleScreen::Run: a session starts (a new game or a load follows);
 * - SaveMenu::Run (the menu between Open and Close, so its cartridge
 *   record is there before and after): the four slots are kept before
 *   it runs; after it, a game loaded from the cartridge's slots
 *   (`gameLoaded`, state LOAD: `cursor` is the slot) makes that slot the
 *   one in use, and one from the link cable's none; a slot that changed
 *   and now holds this game's progress was saved to, and is the one in
 *   use; a slot in use that is now empty was deleted. (LoadInput and
 *   SaveToSlot are called from their own file, which --wrap can't
 *   reach.)
 *
 * The write is the save menu's own (SaveMenu::SaveToSlot): the
 * cartridge's save record read (SaveData::Load, reset as
 * SaveMenu::LoadData resets a bad one), the slot written with the level
 * state's packed progress, its level and the volumes, and stored
 * (SaveData::Store, with its retries); a failed store puts the slot back
 * as it was. The record is the host's own, not the game's heap. */

#include "game/ui/save_menu.hpp"
#include "engine/audio/audio.hpp"
#include "game/level/level_state.hpp"
#include "game/ui/frontend.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "host_ui.h"
}

using Engine::Audio::AudioContext;
using Engine::Save::SaveData;
using Game::Level::GameProgress;
using Game::Ui::SaveMenu;
using Game::Ui::TitleScreen;

/* SaveMenu::instance, private to the class: by its symbol. */
extern SaveMenu *sSaveMenuInstance __asm__("_ZN4Game2Ui8SaveMenu8instanceE");

extern "C" {

s32 __real__ZN4Game2Ui11TitleScreen3RunEv(TitleScreen *self);
u8 __real__ZN4Game2Ui8SaveMenu3RunEjj(u32 state, u32 cursor);

extern "C" int gHostStatsShown; /* level_pick.c */

s32 __wrap__ZN4Game2Ui11TitleScreen3RunEv(TitleScreen *self)
{
    s32 result;

    HostAutosaveSessionStart();
    gHostStatsShown = 0; /* no game under way: no stats in the border */
    result = __real__ZN4Game2Ui11TitleScreen3RunEv(self);
    /* the title left: a run starts (the speedrun timer, timer.c) */
    HostTimerRunStart();
    return result;
}

u8 __wrap__ZN4Game2Ui8SaveMenu3RunEjj(u32 state, u32 cursor)
{
    static struct save_slot before[4];
    u8 emptyBefore[4];
    SaveMenu *menu = sSaveMenuInstance;
    u8 result;
    s32 row;

    if (menu == NULL || menu->cartSave == NULL) {
        return __real__ZN4Game2Ui8SaveMenu3RunEjj(state, cursor);
    }
    for (row = 0; row < 4; row++) {
        emptyBefore[row] = menu->cartSave->IsSlotEmpty(row);
        __builtin_memcpy(&before[row], (u8 *)menu->cartSave + row * sizeof(struct save_slot), sizeof(struct save_slot));
    }
    result = __real__ZN4Game2Ui8SaveMenu3RunEjj(state, cursor);
    if (menu->gameLoaded) {
        HostAutosaveLoaded(menu->state == SAVE_MENU_STATE_LOAD ? menu->cursor : -1);
        return result;
    }
    for (row = 0; row < 4; row++) {
        const u8 *now = (const u8 *)menu->cartSave + row * sizeof(struct save_slot);
        int empty = menu->cartSave->IsSlotEmpty(row);
        int changed = empty != emptyBefore[row] || __builtin_memcmp(now, &before[row], sizeof(struct save_slot)) != 0;

        if (!empty && changed &&
            __builtin_memcmp(now, gLevelState->PackSaveData(), sizeof(GameProgress)) == 0) {
            HostAutosaveSaved(row);
        }
    }
    row = HostAutosaveSlotInUse();
    if (row >= 0 && menu->cartSave->IsSlotEmpty(row)) {
        HostAutosaveSlotGone();
    }
    return result;
}

/* The save menu's write, to slot `row` (0-3): 1 stored, 0 not. */
int HostAutosaveWrite(int row)
{
    static u32 storage[(sizeof(SaveData) + 3) / 4];
    SaveData *save = (SaveData *)storage;
    struct save_slot slot[2]; /* [0] the slot's old contents, [1] the new */
    int had;

    save->Reset();
    if ((u32)(save->Load() - 1) <= 3) {
        save->Reset();
    }
    had = !save->IsSlotEmpty(row);
    if (had) {
        save->ReadSlot(row, &slot[0]);
    }
    __builtin_memset(&slot[1], 0, sizeof(slot[1])); /* the padding too: the same bytes each time */
    __builtin_memcpy(&slot[1].progress, gLevelState->PackSaveData(), sizeof(GameProgress));
    slot[1].level = (u8)gLevelState->GetCurrentLevel();
    slot[1].sfxVolume = (u16)gAudioContext->GetSfxVolume();
    slot[1].musicVolume = (u16)gAudioContext->GetMusicVolume();
    save->WriteSlot(row, &slot[1]);
    if (save->Store() != 0) {
        if (had) {
            save->WriteSlot(row, &slot[0]);
        } else {
            save->EraseSlot(row);
        }
        return 0;
    }
    return 1;
}

} /* extern "C" */
