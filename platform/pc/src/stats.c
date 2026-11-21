/* Play statistics (docs/platforms.md, "Play statistics"): each save's
 * play time, deaths and crates broken, shown on a page of the pause
 * menu's monitor (stats_hooks.cpp).
 *
 * They follow the game's progress as a save does: the game under way
 * has its own (`sCurrent`); loading a slot takes that slot's, a new game
 * (or a game from the link cable) starts from none, and saving to a
 * slot (in the save menu, or an auto save) gives the slot the game's.
 * Deleting a slot clears its. The save menu's moves are autosave_hooks.cpp's
 * (the same that tell auto save which slot is in use).
 *
 * - Play time: the game frames from leaving the title screen, but not
 *   while the pause menu is up (the settings menu holds the game, so its
 *   time isn't counted either).
 * - Deaths: each life lost (LevelState::LoseLife; with infinite lives
 *   too, and in time trials).
 * - Crates broken: each crate the game counts (LevelState::AddBrokenCrate;
 *   a crate broken again after a checkpoint counts again).
 *
 * The four slots' statistics are kept in a small text file beside the
 * save (crashbandicootxs.stats next to crashbandicootxs.sav), so the save
 * itself stays the GBA's own: written each time a slot's statistics change. Headless
 * runs leave the file alone (HOST_PORT_MENUS=1 uses it, for the tests). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#define SLOTS 4

static struct HostPlayStats sSlots[SLOTS];
static struct HostPlayStats sCurrent;
static int sSession; /* a game under way: the title left */
static int sRead;

void HostUpdateFrame(void); /* update_check.c */
int HostPauseMenuUp(void);  /* mouse_hooks.cpp */

static int UseFile(void)
{
    return gHostOptions.save != NULL &&
           (!gHostOptions.headless || getenv("HOST_PORT_MENUS") != NULL);
}

/* <save> with ".stats" for its ".sav" (or after it). */
static void FilePath(char *out, size_t size)
{
    const char *save = gHostOptions.save;
    size_t n = strlen(save);

    if (n > 4 && strcmp(save + n - 4, ".sav") == 0) {
        n -= 4;
    }
    snprintf(out, size, "%.*s.stats", (int)n, save);
}

static void ReadFile(void)
{
    char path[1100];
    char line[160];
    FILE *f;

    if (sRead) {
        return;
    }
    sRead = 1;
    if (!UseFile()) {
        return;
    }
    FilePath(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int slot;
        unsigned long frames;
        unsigned long deaths;
        unsigned long crates;

        if (sscanf(line, "slot%d = %lu %lu %lu", &slot, &frames, &deaths, &crates) == 4 &&
            slot >= 1 && slot <= SLOTS) {
            sSlots[slot - 1].frames = (u32)frames;
            sSlots[slot - 1].deaths = (u32)deaths;
            sSlots[slot - 1].crates = (u32)crates;
        }
    }
    fclose(f);
}

static void WriteFile(void)
{
    char path[1100];
    FILE *f;
    int i;

    if (!UseFile()) {
        return;
    }
    FilePath(path, sizeof(path));
    f = fopen(path, "w");
    if (f == NULL) {
        HostLogf("%s: can't write the play statistics\n", path);
        return;
    }
    fprintf(f, "# N. Larged XS play statistics, one line a save slot:\n"
               "# play time (game frames, about 60 a second), deaths, crates broken\n");
    for (i = 0; i < SLOTS; i++) {
        fprintf(f, "slot%d = %lu %lu %lu\n", i + 1, (unsigned long)sSlots[i].frames,
                (unsigned long)sSlots[i].deaths, (unsigned long)sSlots[i].crates);
    }
    fclose(f);
}

/* Once a game frame (input.c's key read). */
void HostStatsFrame(void)
{
    HostUpdateFrame();
    if (sSession && !HostPauseMenuUp()) {
        sCurrent.frames++;
    }
}

/* The title screen: no game under way. */
void HostStatsTitle(void)
{
    sSession = 0;
    memset(&sCurrent, 0, sizeof(sCurrent));
}

/* The title left: a new game, or the save menu's load. */
void HostStatsRunStart(void)
{
    sSession = 1;
}

/* A game loaded from slot `row` (0-3), or -1 from the link cable. */
void HostStatsLoaded(int row)
{
    ReadFile();
    if (row >= 0 && row < SLOTS) {
        sCurrent = sSlots[row];
    } else {
        memset(&sCurrent, 0, sizeof(sCurrent));
    }
    if (gHostOptions.headless) {
        HostLogf("stats: loaded slot %d: %lu frames, %lu deaths, %lu crates\n", row + 1,
                 (unsigned long)sCurrent.frames, (unsigned long)sCurrent.deaths,
                 (unsigned long)sCurrent.crates);
    }
}

/* The game saved to slot `row`. */
void HostStatsSaved(int row)
{
    if (row < 0 || row >= SLOTS) {
        return;
    }
    ReadFile();
    sSlots[row] = sCurrent;
    WriteFile();
    if (gHostOptions.headless) {
        HostLogf("stats: saved to slot %d: %lu frames, %lu deaths, %lu crates\n", row + 1,
                 (unsigned long)sCurrent.frames, (unsigned long)sCurrent.deaths,
                 (unsigned long)sCurrent.crates);
    }
}

/* Slot `row` deleted. */
void HostStatsErased(int row)
{
    if (row < 0 || row >= SLOTS) {
        return;
    }
    ReadFile();
    memset(&sSlots[row], 0, sizeof(sSlots[row]));
    WriteFile();
}

/* HOST_STATS_LOG=1: each death and crate, at its frame (the tests). */
static int Logging(void)
{
    static int sOn = -1;

    if (sOn < 0) {
        sOn = getenv("HOST_STATS_LOG") != NULL;
    }
    return sOn;
}

void HostStatsDeath(void)
{
    sCurrent.deaths++;
    if (Logging()) {
        HostLogf("stats: death %lu at frame %u\n", (unsigned long)sCurrent.deaths,
                 (unsigned)HostFrame());
    }
}

void HostStatsCrate(u32 n)
{
    sCurrent.crates += n;
    if (Logging()) {
        HostLogf("stats: %lu crates at frame %u\n", (unsigned long)sCurrent.crates,
                 (unsigned)HostFrame());
    }
}

const struct HostPlayStats *HostStatsCurrent(void)
{
    return &sCurrent;
}

/* The play time as h:mm:ss (m:ss under an hour). The GBA's frame rate:
 * 16777216 / 280896 frames a second. */
void HostStatsTimeText(char *out, int size, u32 frames)
{
    u32 seconds = (u32)((unsigned long long)frames * 280896 / 16777216);
    u32 h = seconds / 3600;
    u32 m = seconds / 60 % 60;
    u32 s = seconds % 60;

    if (h != 0) {
        snprintf(out, size, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m,
                 (unsigned long)s);
    } else {
        snprintf(out, size, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
    }
}

void HostStatsNumberText(char *out, int size, u32 n)
{
    snprintf(out, size, "%lu", (unsigned long)n);
}
