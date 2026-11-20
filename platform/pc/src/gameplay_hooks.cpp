/* Gameplay's options that change how Crash plays (docs/platforms.md,
 * "Gameplay options"), each off by default and in headless runs (whose
 * settings are zeros), so the tests play the game as it is.
 *
 * Slide jump grace: as in Crash Bandicoot 2 and 3, a slide that runs off
 * a ledge can still be jumped out of for a moment. In XS the slide's
 * state (ActionCtrl::StateSlide) checks the ground before the A button:
 * the frame the floor goes, it queues the fall (Y entry 5) and never looks
 * at A, and once the slide's animation is done it hands over to the fall
 * (ACTION_STATE_AIRBORNE_FALL, animation 0x1B), whose state
 * (StateAirborne) has no slide jump either. Here, for the setting's frames
 * (off, 4, 6, 8 or 10) after the floor went under a slide (in the slide or
 * the fall that follows it), A held does what the slide does with A on
 * the ground: the high jump (its sound, StartHighJump; the double jump
 * works from it as from the slide's). Room for the jump's animation is
 * checked as the slide checks it. HOST_SLIDE_GRACE=N for the tests.
 *
 * Jump buffer: the game jumps only on the frame A goes down with Crash on
 * the ground, so a press a frame or two before landing is lost. Here a
 * press in the air (one the double jump didn't take) is remembered, and
 * if Crash lands within the setting's frames (off, 4, 6, 8 or 10) with A
 * still held, the landing (StateLand, which hands over to the idle state's
 * jump) sees A pressed on that frame. HOST_JUMP_BUFFER=N for the tests.
 *
 * Relaxed double jump: the game tries the double jump (TryDoubleJump)
 * only while Crash falls at under 2.5 px a frame, near the top of the
 * jump, so a press later on the way down does nothing. Here it is tried
 * for the whole fall: TryDoubleJump's own conditions still hold (the
 * power, once a jump, a jump's animation: not after walking off a ledge,
 * not in the air spin). HOST_LATE_DOUBLE_JUMP=1 for the tests.
 *
 * Infinite lives: LevelState::LoseLife (every death calls it; time trials
 * already lose none) does nothing. HOST_INFINITE_LIVES=1 for the tests.
 *
 * The link wraps the states the slide, the fall and the landing run in,
 * which only ActionCtrl::stateFuncs calls (another object), HandleAirInput
 * and LoseLife (platform.mk, HOST_WRAPS). HOST_SLIDE_LOG=1 logs the slide,
 * the air and the landing each frame; HOST_TEST_DOUBLE_JUMP=1 gives the
 * double jump's power (the tests' runs start without it). */

#include "game/crash/action_ctrl.hpp"
#include "game/crash/player.hpp"
#include "game/level/level_state.hpp"
#include "engine/audio/audio.hpp"

extern "C" {
#include "core.h"
#include "globals.h"
#include "host_platform.h"
#include "host_ui.h"
#include "constants/action_states.h"
#include "constants/progress_flags.h"
}

using Game::Crash::ActionCtrl;
using Game::Crash::Player;
using Game::Level::LevelState;

extern "C" {

static s32 sGrace = -1; /* frames of grace left; -1: not in a slide off a ledge */
static u32 sGraceFrame; /* HostFrame() it was last counted at */

char *getenv(const char *name); /* <stdlib.h> clashes with the game's rand */
int atoi(const char *s);

/* HOST_SLIDE_LOG: each slide and fall frame on stderr (the tests). */
static void Log(const char *where, ActionCtrl *self)
{
    static int sOn = -1;

    if (sOn < 0) {
        sOn = getenv("HOST_SLIDE_LOG") != NULL;
    }
    if (sOn) {
        HostLogf("slide %u %s state %d air %d grace %d y %d lives %d\n", (unsigned)HostFrame(),
                 where, (int)self->state, self->part->hitAxes == 0, (int)sGrace,
                 (int)(self->part->y >> 8), gLevelState != NULL ? (int)gLevelState->lives : -1);
    }
}


static int GraceFrames(void)
{
    static int sEnv = -1;

    if (sEnv < 0) {
        const char *e = getenv("HOST_SLIDE_GRACE");

        sEnv = e != NULL ? atoi(e) : 0;
    }
    if (sEnv > 0) {
        return sEnv;
    }
    {
        static const int frames[HOST_SLIDE_GRACE_COUNT] = { 0, 4, 6, 8, 10 };
        int i = gHostSettings.slideGrace;

        return i > 0 && i < HOST_SLIDE_GRACE_COUNT ? frames[i] : 0;
    }
}

/* An option's setting, or its test variable in headless runs. */
static int Setting(const char *env, int *cache, int value)
{
    if (*cache < 0) {
        const char *e = getenv(env);

        *cache = e != NULL ? atoi(e) : 0;
    }
    return *cache > 0 ? *cache : value;
}

static int BufferFrames(void)
{
    static const int frames[HOST_SLIDE_GRACE_COUNT] = { 0, 4, 6, 8, 10 };
    static int sEnv = -1;
    int i = gHostSettings.jumpBuffer;

    return Setting("HOST_JUMP_BUFFER", &sEnv, i > 0 && i < HOST_SLIDE_GRACE_COUNT ? frames[i] : 0);
}

static int LateDoubleJump(void)
{
    static int sEnv = -1;

    return Setting("HOST_LATE_DOUBLE_JUMP", &sEnv, gHostSettings.lateDoubleJump);
}

/* The jump buffer: when A was last pressed in the air (0: none to keep). */
static u32 sBufferedAt;

/* A held while the grace lasts: the slide's high jump, and 1. */
static int GraceJump(ActionCtrl *self)
{
    Player *p = self->part;
    u32 now = HostFrame();

    if (sGrace < 0 || now - sGraceFrame > 1) {
        sGrace = -1;
        return 0;
    }
    sGraceFrame = now;
    if (sGrace == 0) {
        sGrace = -1;
        return 0;
    }
    sGrace--;
    if ((gKeys.half.held & A_BUTTON) && p->HasRoomForAnim(0xB) == 1) {
        sGrace = -1;
        gAudioContext->PlaySfx(SFX_HIGH_JUMP, 0x100);
        Game::Crash::ActAndFlags0D(p, ~ENTITY_FLAG2_FLOOR_PROBE);
        Game::Crash::ActAndFlags0D(p, ~ENTITY_FLAG2_GROUNDED);
        self->StartHighJump();
        Log("JUMP", self);
        return 1;
    }
    return 0;
}

void __real__ZN4Game5Crash10ActionCtrl10StateSlideEv(ActionCtrl *self);

/* Aligned: the state table holds them as member function pointers, whose
 * low bit set means a virtual one (the C++ ABI), so they must start at an
 * even address, as a member function does. */
__attribute__((aligned(4))) void __wrap__ZN4Game5Crash10ActionCtrl10StateSlideEv(ActionCtrl *self);
__attribute__((aligned(4))) void
__wrap__ZN4Game5Crash10ActionCtrl13StateAirborneEv(ActionCtrl *self);
__attribute__((aligned(4))) void __wrap__ZN4Game5Crash10ActionCtrl9StateLandEv(ActionCtrl *self);

void __wrap__ZN4Game5Crash10ActionCtrl10StateSlideEv(ActionCtrl *self)
{
    int grace = GraceFrames();

    Log("slide", self);
    if (grace > 0) {
        if (self->part->hitAxes != 0) {
            /* on the ground: the grace starts when it goes */
            sGrace = grace;
            sGraceFrame = HostFrame();
        } else if (GraceJump(self)) {
            return;
        }
    }
    __real__ZN4Game5Crash10ActionCtrl10StateSlideEv(self);
}

void __real__ZN4Game5Crash10ActionCtrl13StateAirborneEv(ActionCtrl *self);

void __wrap__ZN4Game5Crash10ActionCtrl13StateAirborneEv(ActionCtrl *self)
{
    s32 jumps = self->frame;
    int pressed = (gKeys.half.pressed & A_BUTTON) != 0;

    static int sTestPower = -1;

    /* HOST_TEST_DOUBLE_JUMP: the power, for the relaxed double jump's tests */
    if (Setting("HOST_TEST_DOUBLE_JUMP", &sTestPower, 0) && gLevelState != NULL) {
        gLevelState->progress.flags |= PROGRESS_FLAG_DOUBLE_JUMP;
    }
    Log("air", self);
    /* the fall the slide hands over to */
    if (self->state == ACTION_STATE_AIRBORNE_FALL && sGrace >= 0 && GraceJump(self)) {
        sBufferedAt = 0;
        return;
    }
    __real__ZN4Game5Crash10ActionCtrl13StateAirborneEv(self);
    /* a press the double jump didn't take: for the landing */
    if (pressed && BufferFrames() > 0) {
        sBufferedAt = self->frame != jumps ? 0 : HostFrame();
    }
}

void __real__ZN4Game5Crash10ActionCtrl9StateLandEv(ActionCtrl *self);

void __wrap__ZN4Game5Crash10ActionCtrl9StateLandEv(ActionCtrl *self)
{
    int frames = BufferFrames();
    int add = 0;

    if (frames > 0 && sBufferedAt != 0 && HostFrame() - sBufferedAt <= (u32)frames &&
        (gKeys.half.held & A_BUTTON) && !(gKeys.half.pressed & A_BUTTON)) {
        gKeys.half.pressed |= A_BUTTON; /* the idle state's jump sees it */
        add = 1;
        Log("BUFFERED", self);
    }
    Log("land", self);
    sBufferedAt = 0;
    __real__ZN4Game5Crash10ActionCtrl9StateLandEv(self);
    if (add) {
        gKeys.half.pressed &= ~A_BUTTON;
    }
}

void __real__ZN4Game5Crash10ActionCtrl14HandleAirInputEv(ActionCtrl *self);

void __wrap__ZN4Game5Crash10ActionCtrl14HandleAirInputEv(ActionCtrl *self)
{
    /* past the top, where the game no longer tries the double jump */
    if (LateDoubleJump() && self->part->speedY > Q8(2.5) - 1 && self->TryDoubleJump()) {
        Log("LATE DOUBLE", self);
        return;
    }
    __real__ZN4Game5Crash10ActionCtrl14HandleAirInputEv(self);
}

void __real__ZN4Game5Level10LevelState8LoseLifeEv(LevelState *self);

void __wrap__ZN4Game5Level10LevelState8LoseLifeEv(LevelState *self)
{
    static int sEnv = -1;

    if (Setting("HOST_INFINITE_LIVES", &sEnv, gHostSettings.infiniteLives)) {
        return;
    }
    __real__ZN4Game5Level10LevelState8LoseLifeEv(self);
}
}
