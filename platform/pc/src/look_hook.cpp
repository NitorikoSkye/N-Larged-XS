/* Looking up and down (docs/platforms.md, "Look up and down"; Gameplay's
 * Look up/down): holding up or down for a moment while Crash stands
 * still on the ground moves the view that way, as metroidvanias do, and
 * letting go brings it back.
 *
 * The link wraps Camera::Update (platform.mk, HOST_WRAPS; only
 * UpdateRoomFrame, play_room.cpp, calls it: the rooms on foot and under
 * water, not the vehicle rooms). After the game's own update, which
 * hands its scroll to gLevelLayers->SetScroll, the scroll is set again
 * with the look's offset added: the game's camera itself never moves,
 * SetScroll clamps the view to the level as always, and the layers'
 * Scroll (which streams the BG's new rows) and the sprites' drawing come
 * after it in the same frame. Only the camera that follows Crash's facing
 * (on foot) looks; under water the camera already goes where Crash swims.
 *
 * HOST_LOOK=1 turns it on in headless runs, whose settings are zeros
 * (off), so the tests' pictures stay the game's. */

#include "game/level/camera.hpp"
#include "game/level/bg_layer.hpp"
#include "game/crash/player.hpp"

extern "C" {
#include <stdlib.h>

#include "core.h"
#include "globals.h"
#include "host_platform.h"
#include "host_ui.h"
}

using Game::Level::Camera;

/* How long up or down is held before the view moves, how far it goes and
 * how fast (a fraction of the remaining distance each frame). */
enum {
    LOOK_DELAY = 40,             /* frames: two thirds of a second */
    LOOK_DISTANCE = INT_TO_Q8(48), /* 30% of the screen's height */
    LOOK_EASE = 8,
};

extern "C" {

s32 gHostLookOffset; /* Q8, the view's offset now (for the tests) */

void __real__ZN4Game5Level6Camera6UpdateEv(Camera *self);

void __wrap__ZN4Game5Level6Camera6UpdateEv(Camera *self)
{
    static s32 sHeld;      /* frames up (<0) or down (>0) has been held */
    static s32 sOffset;    /* Q8 */
    static u32 sLastFrame; /* to start afresh after a room change or a pause */
    static int sEnv = -1;
    u16 keys = gKeys.half.held;
    s32 want = 0;
    s32 dir = 0;
    u32 frame = HostFrame();

    __real__ZN4Game5Level6Camera6UpdateEv(self);

    if (sEnv < 0) {
        sEnv = getenv("HOST_LOOK") != NULL && atoi(getenv("HOST_LOOK")) != 0;
    }
    if (frame - sLastFrame > 2) {
        sHeld = 0;
        sOffset = 0;
    }
    sLastFrame = frame;
    if (!gHostSettings.lookAround && !sEnv) {
        sHeld = 0;
        sOffset = 0;
        gHostLookOffset = 0;
        return;
    }

    /* Up or down alone, standing still on the ground, the camera
     * following Crash on foot. */
    if (self->mode == CAMERA_MODE_FACING && gPlayer != NULL && self->target == gPlayer && (gPlayer->hitAxes & PART_DIR_DOWN) && gPlayer->speedY == 0 &&
        gPlayer->speedX == 0 && !(keys & (DPAD_LEFT | DPAD_RIGHT | A_BUTTON | B_BUTTON | L_BUTTON | R_BUTTON))) {
        if ((keys & (DPAD_UP | DPAD_DOWN)) == DPAD_UP) {
            dir = -1;
        } else if ((keys & (DPAD_UP | DPAD_DOWN)) == DPAD_DOWN) {
            dir = 1;
        }
    }
    if (dir == 0 || (sHeld != 0 && (sHeld < 0) != (dir < 0))) {
        sHeld = 0;
    }
    if (dir != 0 && sHeld * dir < LOOK_DELAY) {
        sHeld += dir;
    }
    if (dir != 0 && sHeld * dir >= LOOK_DELAY) {
        want = dir * LOOK_DISTANCE;
    }
    if (want != sOffset) {
        s32 step = (want - sOffset) / LOOK_EASE;

        if (step == 0) {
            step = want > sOffset ? 1 : -1;
        }
        sOffset += step;
    }
    gHostLookOffset = sOffset;
    if (sOffset != 0) {
        gLevelLayers->SetScroll(self->x - INT_TO_Q8(DISPLAY_CENTER_X), self->y + sOffset - INT_TO_Q8(DISPLAY_CENTER_Y));
    }
}
}
