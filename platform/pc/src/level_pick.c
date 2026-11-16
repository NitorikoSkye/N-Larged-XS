#include <stdio.h>
/* --level N and --room R (main.c): the level select's choice is level N,
 * and the level starts in its room R, whatever the keys picked. The same
 * as tools/boottest.py's level scripts do on mGBA (setting
 * LevelSelectLoop's return and SelectRoom's roomIndex), so that a key
 * file can play any stage on both: a polar or jetpack stage, a boss.
 *
 * The link wraps the two (platform.mk, --wrap): LevelSelect::Run, which
 * stores LevelSelect::Loop's choice in its argument (game_frame.cpp's
 * `room.level`), and LevelProgress::SelectRoom (room_select.cpp), which
 * loads the room roomIndex names. Each override is used once: the level
 * at the first level select, the room at the first SelectRoom after it.
 *
 * --room bonus and --room gem-path start the level in its bonus or
 * gem-path room instead (tools/preview.py's, the level editor's "Play this
 * room"): the level's room list has them as extra1 and extra2, which the
 * game only enters from a room (UpdateGameFrame: EnterBonusRoom or
 * EnterGemPathRoom when the bonus platform or the gem path is taken). The
 * first SelectRoom picks its room as the game does, then the room block's
 * own EnterBonusRoom or EnterGemPathRoom makes that extra room the current
 * one, and the level plays it. The game isn't in its bonus round or on its
 * gem path (LevelState's flags, which the platform sets), so the player
 * starts at the room's start marker without the warp-in, and the room's
 * exit goes on to the level's first room. */

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

/* LevelProgress's first fields (engine/include/game/level/level_state.hpp):
 * two s32s, at the same offsets on any host. */
struct LevelProgressHead {
    s32 level;
    s32 roomIndex;
};

s32 __real__ZN4Game2Ui11LevelSelect3RunEPi(s32 *level);
_Bool __real__ZN4Game5Level13LevelProgress10SelectRoomEv(struct LevelProgressHead *self);
void _ZN4Game5Level13LevelProgress14EnterBonusRoomEv(struct LevelProgressHead *self);
void _ZN4Game5Level13LevelProgress16EnterGemPathRoomEv(struct LevelProgressHead *self);

static int sLevelPicked;
static int sRoomPicked;

s32 __wrap__ZN4Game2Ui11LevelSelect3RunEPi(s32 *level)
{
    s32 result;

    /* Back in the warp room: the level's split (timer.c) and auto save
     * (autosave.c), as it starts. */
    HostTimerWarpRoom();
    HostAutosaveWarpRoom();
    /* The warp room, for Discord's status (discord.c). */
    gHostInWarpRoom = 1;
    result = __real__ZN4Game2Ui11LevelSelect3RunEPi(level);
    gHostInWarpRoom = 0;

    if (gHostOptions.level >= 0 && !sLevelPicked) {
        sLevelPicked = 1;
        *level = gHostOptions.level;
    }
    return result;
}

_Bool __wrap__ZN4Game5Level13LevelProgress10SelectRoomEv(struct LevelProgressHead *self)
{
    _Bool selected;
    s32 extra = HOST_EXTRA_ROOM_NONE;

    if (sLevelPicked && !sRoomPicked) {
        if (gHostOptions.room >= 0) {
            sRoomPicked = 1;
            self->roomIndex = gHostOptions.room;
        } else if (gHostOptions.extraRoom != HOST_EXTRA_ROOM_NONE) {
            sRoomPicked = 1;
            extra = gHostOptions.extraRoom;
        }
    }
    selected = __real__ZN4Game5Level13LevelProgress10SelectRoomEv(self);
    if (extra == HOST_EXTRA_ROOM_BONUS) {
        _ZN4Game5Level13LevelProgress14EnterBonusRoomEv(self);
    } else if (extra == HOST_EXTRA_ROOM_GEM_PATH) {
        _ZN4Game5Level13LevelProgress16EnterGemPathRoomEv(self);
    }
    return selected;
}

/* The level being played, for the themed borders (borders.c): set while
 * LevelProgress::PlayRoom runs a room of it, -1 otherwise (the menus, the
 * level select, the cutscenes). */
s32 gHostPlayingLevel = -1;

s32 __real__ZN4Game5Level13LevelProgress8PlayRoomEv(struct LevelProgressHead *self);

s32 __wrap__ZN4Game5Level13LevelProgress8PlayRoomEv(struct LevelProgressHead *self)
{
    s32 result;

    gHostPlayingLevel = self->level;
    result = __real__ZN4Game5Level13LevelProgress8PlayRoomEv(self);
    gHostPlayingLevel = -1;
    return result;
}

/* HOST_PRINT_PROGRESS (tests): the progress the themed borders' stats
 * panel shows, at the end of a run. */
extern void *gLevelState;
s32 CountCrystals(const void *progress);
s32 CountGems(const void *progress);
s32 CountRelics(const void *progress);
s32 GetCompletionPercent(const void *progress);

void HostPrintProgress(void)
{
    const u8 *p = gLevelState;

    if (p != NULL) {
        printf("progress: %d%% crystals %d gems %d relics %d flags 0x%02x\n", GetCompletionPercent(p),
               CountCrystals(p), CountGems(p), CountRelics(p), p[2]);
    }
}
