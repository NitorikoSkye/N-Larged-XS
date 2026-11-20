/* The Android app's touch buttons' icons (docs/platforms.md, "Android";
 * HOST_ANDROID builds): what A, B, L and R do right now, for the app to
 * draw on them (stream.c's message 7; GameView.java).
 *
 * On foot, from the action controller's state (gPlayer->mover): A jumps
 * on the ground and is the double jump in the air while it can still be
 * done (the power, no jump of it yet, a jump's animation, not in the air
 * spin, and, with Gameplay's double jump normal, not falling at 2.5 px a
 * frame or more: HandleAirInput's own conditions); R crouches standing,
 * slides running and is the body slam in the air; L is the turbo run once
 * its power is had, on the ground. Underwater (SwimCtrl) A swims a stroke
 * and B and R spin; on the polar ride A jumps and B dashes; with the
 * jetpack A fires and L and R roll; on the hover levels only the D-pad
 * steers. A button with nothing to do now is sent dimmed (HOST_ICON_DIM)
 * with the icon it has when it can.
 *
 * States are read, not the terrain's contact (which flickers on slopes),
 * and a set must hold for three frames before it is sent, so a bump in
 * the ground or a one-frame state doesn't blink the buttons (the first
 * set goes at once). Touch's icons "fixed" sends the original four, never
 * dimmed. */

#include "game/crash/action_ctrl.hpp"
#include "game/crash/player.hpp"
#include "game/level/level_state.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "host_platform.h"
#include "host_ui.h"
#include "constants/action_states.h"
}

using Game::Crash::ActionCtrl;
using Game::Crash::Player;

extern "C" {

extern int gHostCategoryType; /* hud_hook.cpp */
/* <string.h> and <stdlib.h> clash with the game's own */
int memcmp(const void *a, const void *b, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
char *getenv(const char *name);
int HostLateDoubleJump(void); /* gameplay_hooks.cpp */

static int Airborne(s32 s)
{
    switch (s) {
    case ACTION_STATE_JUMP:
    case ACTION_STATE_AIRBORNE_JUMP:
    case ACTION_STATE_BODY_SLAM_START:
    case ACTION_STATE_AIRBORNE_FLIP_JUMP:
    case ACTION_STATE_FLIP_BODY_SLAM_START:
    case ACTION_STATE_AIRBORNE_HIGH_JUMP:
    case ACTION_STATE_AIR_SPIN:
    case ACTION_STATE_AIRBORNE_BODY_SLAM:
    case ACTION_STATE_AIRBORNE_SUPER_BODY_SLAM:
    case ACTION_STATE_AIRBORNE_FALL:
    case ACTION_STATE_LEFT_GROUND:
        return 1;
    default:
        return 0;
    }
}

static int Slamming(s32 s)
{
    return s == ACTION_STATE_BODY_SLAM_START || s == ACTION_STATE_FLIP_BODY_SLAM_START ||
           s == ACTION_STATE_AIRBORNE_BODY_SLAM || s == ACTION_STATE_AIRBORNE_SUPER_BODY_SLAM;
}

static void OnFoot(u8 icons[4])
{
    ActionCtrl *a = (ActionCtrl *)gPlayer->mover;
    Player *p = a->part;
    s32 s = a->state;
    int turbo = gLevelState->HasTurboRun();

    icons[0] = HOST_ICON_JUMP;
    icons[1] = HOST_ICON_SPIN;
    icons[2] = HOST_ICON_TURBO | (turbo ? 0 : HOST_ICON_DIM);
    icons[3] = HOST_ICON_CROUCH;
    if (s == ACTION_STATE_DYING || s == ACTION_STATE_WARP_OUT || s == ACTION_STATE_WARP_IN) {
        icons[0] |= HOST_ICON_DIM;
        icons[1] |= HOST_ICON_DIM;
        icons[2] |= HOST_ICON_DIM;
        icons[3] |= HOST_ICON_DIM;
        return;
    }
    if (s >= ACTION_STATE_HANG_GRAB && s <= ACTION_STATE_HANG_STOP) {
        /* hanging: B spins, down or R lets go */
        icons[0] |= HOST_ICON_DIM;
        icons[2] |= HOST_ICON_DIM;
        return;
    }
    if (Airborne(s)) {
        /* the takeoff (ACTION_STATE_JUMP, a few frames before the
         * jump's animation) counts: it's a moment away, and the button
         * doesn't blink faint on every jump */
        int can = a->frame == 0 && s != ACTION_STATE_AIR_SPIN && !Slamming(s) &&
                  (s == ACTION_STATE_JUMP || p->tag == 6 || p->tag == 0xB || p->tag == 0xC) &&
                  (HostLateDoubleJump() || p->speedY <= Q8(2.5) - 1);

        if (gLevelState->HasDoubleJump()) {
            icons[0] = HOST_ICON_DOUBLE_JUMP | (can ? 0 : HOST_ICON_DIM);
        } else {
            icons[0] |= HOST_ICON_DIM;
        }
        icons[2] |= HOST_ICON_DIM;
        icons[3] = HOST_ICON_SLAM;
        if (s == ACTION_STATE_AIR_SPIN || s == ACTION_STATE_AIRBORNE_HIGH_JUMP || Slamming(s)) {
            icons[3] |= HOST_ICON_DIM;
        }
        return;
    }
    if (s == ACTION_STATE_RUN || s == ACTION_STATE_TURBO_RUN || s == ACTION_STATE_SLIDE) {
        icons[3] = HOST_ICON_SLIDE;
    }
}

/* This frame's icons for A, B, L and R. */
static void Now(u8 icons[4])
{
    icons[0] = HOST_ICON_JUMP;
    icons[1] = HOST_ICON_SPIN;
    icons[2] = HOST_ICON_TURBO;
    icons[3] = HOST_ICON_SLAM;
    if (gHostSettings.touchIcons == HOST_TOUCH_ICONS_FIXED) {
        return;
    }
    if (gHostCategoryType == 0) {
        /* the polar ride */
        icons[1] = HOST_ICON_DASH;
        icons[2] |= HOST_ICON_DIM;
        icons[3] |= HOST_ICON_DIM;
        return;
    }
    if (gHostCategoryType > 0) {
        /* the jetpack */
        icons[0] = HOST_ICON_SHOOT;
        icons[1] |= HOST_ICON_DIM;
        icons[2] = HOST_ICON_ROLL_LEFT;
        icons[3] = HOST_ICON_ROLL_RIGHT;
        return;
    }
    if (gPlayer == NULL || gPlayer->mover == NULL) {
        return;
    }
    switch (gPlayer->ctrlMode) {
    case PLAYER_CTRL_MODE_ACTION:
        OnFoot(icons);
        break;
    case PLAYER_CTRL_MODE_SWIM:
        icons[0] = HOST_ICON_STROKE;
        icons[2] |= HOST_ICON_DIM;
        icons[3] = HOST_ICON_SPIN;
        break;
    default:
        /* the hover levels: the D-pad alone */
        icons[0] |= HOST_ICON_DIM;
        icons[1] |= HOST_ICON_DIM;
        icons[2] |= HOST_ICON_DIM;
        icons[3] |= HOST_ICON_DIM;
        break;
    }
}

/* Whether the icons to send changed (into `out`): called each frame while
 * a level plays (stream.c). */
int HostTouchIcons(u8 out[4], int playing)
{
    static u8 sSent[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    static u8 sSeen[4];
    static int sSeenFrames;
    u8 now[4];
    int i;

    if (!playing) {
        sSeenFrames = 0;
        return 0;
    }
    Now(now);
    if (memcmp(now, sSeen, 4) != 0) {
        memcpy(sSeen, now, 4);
        sSeenFrames = 0;
    }
    sSeenFrames++;
    if (memcmp(now, sSent, 4) == 0 || (sSent[0] != 0xFF && sSeenFrames < 3)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        out[i] = sSent[i] = now[i];
    }
    if (getenv("HOST_TOUCH_ICONS_LOG") != NULL) {
        HostLogf("touch icons %u: %02x %02x %02x %02x\n", HostFrame(), out[0], out[1], out[2],
                 out[3]);
    }
    return 1;
}
}
