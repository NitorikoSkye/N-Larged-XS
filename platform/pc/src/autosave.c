/* Auto save (docs/platforms.md, "Auto save"; Gameplay's Auto save): the
 * game saved for the player when they come back to the warp room with
 * something new.
 *
 * When: each time the warp room starts (the LevelSelect::Run wrap,
 * level_pick.c), if what was earned (completion, crystals, gems, relics,
 * the coloured gems and powers: the pause menu's counts, not the lives)
 * differs from the session's last known state: the game loaded, the
 * last save, or a new game's first visit to the warp room. A level left
 * by dying or quitting earns nothing, and nothing is written.
 *
 * Where: "slot in use", the slot the game was loaded from or last saved
 * to in the save menu (autosave_hooks.cpp follows the menu; a new game
 * has none until its first manual save), or "slot 4", always the fourth,
 * kept for auto saves. Before each write the save file is copied to
 * <save>.bak (eeprom.c). The write is the save menu's own (autosave_hooks.cpp);
 * a corner message says how it went (sdl.c). */

#include <stdio.h>
#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

extern void *gLevelState;
s32 CountCrystals(const void *progress);
s32 CountGems(const void *progress);
s32 CountClearGems(const void *progress);
s32 CountRelics(const void *progress);
s32 CountSapphireRelics(const void *progress);
s32 CountGoldRelics(const void *progress);
s32 CountPlatinumRelics(const void *progress);
s32 GetCompletionPercent(const void *progress);

struct Earned {
    s32 percent;
    s32 crystals;
    s32 gems;
    s32 clearGems;
    s32 relics;
    s32 sapphire;
    s32 gold;
    s32 platinum;
    s32 flags; /* coloured gems and powers */
};

/* --auto-save: the mode, over the setting (-1: the setting's). */
int gHostAutosaveForce = -1;

static s32 sSlot = -1;

static int Mode(void)
{
    return gHostAutosaveForce >= 0 ? gHostAutosaveForce : gHostSettings.autoSave;
}
static int sHaveBase;
static struct Earned sBase;

static void ReadEarned(struct Earned *e)
{
    const u8 *p = gLevelState;

    memset(e, 0, sizeof(*e));
    e->percent = GetCompletionPercent(p);
    e->crystals = CountCrystals(p);
    e->gems = CountGems(p);
    e->clearGems = CountClearGems(p);
    e->relics = CountRelics(p);
    e->sapphire = CountSapphireRelics(p);
    e->gold = CountGoldRelics(p);
    e->platinum = CountPlatinumRelics(p);
    e->flags = p[2];
}

void HostAutosaveSessionStart(void)
{
    sSlot = -1;
    sHaveBase = 0;
}

void HostAutosaveLoaded(int row)
{
    sSlot = row;
    if (gHostOptions.headless) {
        printf("autosave slot in use: %d (loaded)\n", row + 1);
    }
    if (gLevelState != NULL) {
        ReadEarned(&sBase);
        sHaveBase = 1;
    }
}

void HostAutosaveSaved(int row)
{
    sSlot = row;
    if (gHostOptions.headless) {
        printf("autosave slot in use: %d (saved)\n", row + 1);
    }
    /* Slot 4's auto saves follow the progress on their own: a manual save
     * elsewhere doesn't make slot 4 current. */
    if (Mode() != HOST_AUTOSAVE_SLOT4 && gLevelState != NULL) {
        ReadEarned(&sBase);
        sHaveBase = 1;
    }
}

int HostAutosaveSlotInUse(void)
{
    return sSlot;
}

void HostAutosaveSlotGone(void)
{
    sSlot = -1;
}

void HostAutosaveWarpRoom(void)
{
    struct Earned now;
    int row;
    char text[48];

    if (gLevelState == NULL) {
        return;
    }
    if (gHostOptions.headless && gHostAutosaveForce >= 0) {
        printf("autosave warp room at frame %u\n", (unsigned)HostFrame());
    }
    ReadEarned(&now);
    if (!sHaveBase) {
        /* a new game's first visit: where it starts from */
        sBase = now;
        sHaveBase = 1;
        return;
    }
    if (Mode() == HOST_AUTOSAVE_OFF || memcmp(&now, &sBase, sizeof(now)) == 0) {
        return;
    }
    row = Mode() == HOST_AUTOSAVE_SLOT4 ? 3 : sSlot;
    if (row < 0) {
        return;
    }
    HostSaveBackup();
    if (HostAutosaveWrite(row)) {
        HostStatsSaved(row);
        sBase = now;
        if (Mode() == HOST_AUTOSAVE_IN_USE) {
            sSlot = row;
        }
        snprintf(text, sizeof(text), "auto saved to slot %d", row + 1);
    } else {
        snprintf(text, sizeof(text), "auto save failed");
    }
    HostToast(text);
    if (gHostOptions.headless) {
        printf("autosave %s\n", text);
    }
}
