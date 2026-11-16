/* The speedrun timer (docs/platforms.md, "Speedrun timer"; Interface's
 * Speedrun timer): the level's time, the run's, and each level's split
 * against the best one, in a corner of the window (sdl.c draws them).
 *
 * Time is the GBA's: VBlanks, 59.7275 a second (280896 cycles of
 * 16.78 MHz), so the frame rate, vsync and slow computers don't change
 * it, and the settings menu (the game held) isn't counted. The game's own
 * pause menu is, as a run's clock would.
 *
 * - The run starts when the title screen is left (a new game or a load:
 *   the TitleScreen::Run wrap, autosave_hooks.cpp).
 * - A level starts at its first room after the warp room (the PlayRoom
 *   wrap, level_pick.c) and ends back in the warp room (the
 *   LevelSelect::Run wrap): if something was earned in it (completion,
 *   crystals, gems, relics, powers: autosave.c's counts), that is its
 *   split, kept with the best one for the level in splits.ini (the data
 *   folder's); a level left by dying or quitting has no split.
 * - The split shows for 8 seconds: the level, its time, and the
 *   difference from the best before it ("pb" for a first or a new best). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#define VBLANKS_PER_SECOND 59.7275
#define LEVELS 32
#define SPLIT_SHOW 478 /* 8 seconds of VBlanks */

extern const int gUiLevelCount;
extern const char *const gUiLevelNames[];
extern void *gLevelState;
extern s32 gHostPlayingLevel;
s32 CountCrystals(const void *progress);
s32 CountGems(const void *progress);
s32 CountRelics(const void *progress);
s32 GetCompletionPercent(const void *progress);

static u32 sNow;          /* VBlanks counted */
static u32 sRunStart;
static int sRunOn;
static int sLevel = -1;   /* the level being timed */
static u32 sLevelStart;
static u32 sEarnedAtStart;
static u32 sBest[LEVELS]; /* 0: none */
static int sBestLoaded;

static int sSplitLevel = -1;
static u32 sSplitTime;
static s32 sSplitDelta; /* VBlanks; INT32_MIN: a first or new best */
static int sSplitPb;
static u32 sSplitAt;

static u32 Earned(void)
{
    const u8 *p = gLevelState;

    if (p == NULL) {
        return 0;
    }
    /* a fingerprint of what the counts are */
    return (u32)GetCompletionPercent(p) * 1000003u + (u32)CountCrystals(p) * 10007u + (u32)CountGems(p) * 101u +
           (u32)CountRelics(p) * 7u + p[2] * 131071u;
}

#ifdef HOST_SDL
static void SplitsPath(char *out, size_t size)
{
    snprintf(out, size, "%ssplits.ini", HostConfigDataDir());
}

static void LoadBest(void)
{
    char path[1100];
    char line[128];
    FILE *f;

    sBestLoaded = 1;
    SplitsPath(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int level;
        unsigned v;

        if (sscanf(line, "level_%d=%u", &level, &v) == 2 && level >= 0 && level < LEVELS) {
            sBest[level] = v;
        }
    }
    fclose(f);
}

static void SaveBest(void)
{
    char path[1100];
    FILE *f;
    int i;

    SplitsPath(path, sizeof(path));
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# %s speedrun timer: each level's best time, in GBA frames (59.7275 a second).\n",
            HOST_PORT_NAME);
    for (i = 0; i < LEVELS; i++) {
        if (sBest[i] != 0) {
            fprintf(f, "level_%d=%u\n", i, (unsigned)sBest[i]);
        }
    }
    fclose(f);
}
#else
static void LoadBest(void)
{
    sBestLoaded = 1;
}

static void SaveBest(void)
{
}
#endif

void HostTimerVBlank(void)
{
    sNow++;
    if (gHostPlayingLevel >= 0 && gHostPlayingLevel != sLevel && !gHostInWarpRoom) {
        /* a level's first room */
        sLevel = gHostPlayingLevel;
        sLevelStart = sNow;
        sEarnedAtStart = Earned();
    }
}

void HostTimerRunStart(void)
{
    sRunOn = 1;
    sRunStart = sNow;
    sLevel = -1;
    sSplitLevel = -1;
}

void HostTimerWarpRoom(void)
{
    u32 t;

    if (sLevel < 0) {
        return;
    }
    t = sNow - sLevelStart;
    if (Earned() != sEarnedAtStart && sLevel < LEVELS) {
        if (!sBestLoaded) {
            LoadBest();
        }
        sSplitLevel = sLevel;
        sSplitTime = t;
        sSplitAt = sNow;
        sSplitPb = sBest[sLevel] == 0 || t < sBest[sLevel];
        sSplitDelta = sBest[sLevel] == 0 ? 0 : (s32)t - (s32)sBest[sLevel];
        if (sSplitPb) {
            sBest[sLevel] = t;
            SaveBest();
        }
    }
    sLevel = -1;
}

/* m:ss.cc, or h:mm:ss.cc */
static void FormatTime(char *out, size_t size, u32 vblanks, int sign)
{
    unsigned long long cs = (unsigned long long)(vblanks * 100.0 / VBLANKS_PER_SECOND + 0.5);
    unsigned long long s = cs / 100;
    const char *pre = sign > 0 ? "+" : sign < 0 ? "-" : "";

    if (s >= 3600) {
        snprintf(out, size, "%s%llu:%02llu:%02llu.%02llu", pre, s / 3600, s / 60 % 60, s % 60, cs % 100);
    } else {
        snprintf(out, size, "%s%llu:%02llu.%02llu", pre, s / 60, s % 60, cs % 100);
    }
}

/* What to show: up to 3 lines (big, small, small); the number of lines. */
int HostTimerLines(char big[32], char line2[64], char line3[64])
{
    char t[32];
    char d[32];
    int n = 0;

    big[0] = line2[0] = line3[0] = '\0';
    if (gHostSettings.speedrunTimer == HOST_TIMER_OFF) {
        return 0;
    }
    if (sLevel >= 0) {
        FormatTime(big, 32, sNow - sLevelStart, 0);
        n = 1;
    } else if (sSplitLevel >= 0 && sNow - sSplitAt < SPLIT_SHOW) {
        FormatTime(big, 32, sSplitTime, 0);
        n = 1;
    }
    if (gHostSettings.speedrunTimer == HOST_TIMER_RUN && sRunOn) {
        FormatTime(t, sizeof(t), sNow - sRunStart, 0);
        snprintf(line2, 64, "run %s", t);
        n = n < 1 ? 1 : n;
        if (big[0] == '\0') {
            snprintf(big, 32, "%s", t);
            line2[0] = '\0';
        } else {
            n = 2;
        }
    }
    if (sSplitLevel >= 0 && sNow - sSplitAt < SPLIT_SHOW && sSplitLevel < gUiLevelCount) {
        if (sSplitPb) {
            snprintf(d, sizeof(d), "%s", "pb!");
            if (sSplitDelta < 0) {
                FormatTime(t, sizeof(t), (u32)-sSplitDelta, -1);
                snprintf(d, sizeof(d), "pb! %s", t);
            }
        } else {
            FormatTime(d, sizeof(d), (u32)sSplitDelta, 1);
        }
        snprintf(line3, 64, "%s %s", gUiLevelNames[sSplitLevel], d);
        n = 3;
        if (line2[0] == '\0') {
            snprintf(line2, 64, "%s", line3);
            line3[0] = '\0';
            n = 2;
        }
    }
    return n;
}
