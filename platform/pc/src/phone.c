/* The Android app's settings (docs/platforms.md, "Android"; HOST_ANDROID
 * builds only): the PC port's settings menu (ui.c) with the phone's own
 * set of options, kept in settings.ini in the folder the app names
 * (CRASHXS_DIR: its private files, next to the save).
 *
 * The core keeps them: the menu is the core's, drawn in the game's style.
 * What the app needs to know (the touch controls' size and opacity, the
 * haptics, the picture's scaling and filter, the frame rate) goes to it
 * in a settings message (stream.c) whenever they change. */

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#ifdef HOST_ANDROID

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void HostStreamSettingsChanged(void); /* stream.c */
int HostStreamDisplayHz(void);        /* stream.c */
extern u32 gHostGaxVolume[2];         /* audio.c */
extern int gHostAudioHq;              /* gax_dsp.c */

struct HostSettings gHostSettings;

void HostConfigDefaults(struct HostSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->frameRate = HOST_FPS_60;
    s->scaling = HOST_SCALING_FIT;
    s->filter = HOST_FILTER_PIXELS;
    s->master = 100;
    s->music = 100;
    s->sfx = 100;
    s->audioQuality = 1;
    s->autoSave = HOST_AUTOSAVE_IN_USE;
    s->lookAround = 1;
    s->borders = HOST_BORDERS_THEMED;
    s->touchSize = 1;
    s->touchOpacity = 60;
    s->haptics = 1;
    HostConfigDefaultKeys(s);
    HostConfigDefaultPad(s);
}

/* The phone's controllers are the app's to map (MainActivity), as the
 * GBA's buttons by place; the prompts use these. */
void HostConfigDefaultKeys(struct HostSettings *s)
{
    memset(s->keys, 0, sizeof(s->keys));
}

void HostConfigDefaultPad(struct HostSettings *s)
{
    memset(s->pad, 0, sizeof(s->pad));
    s->pad[HOST_BIND_A][0] = HOST_PAD_BUTTON(1);   /* SDL's B: the right face button */
    s->pad[HOST_BIND_B][0] = HOST_PAD_BUTTON(0);   /* SDL's A: the bottom one */
    s->pad[HOST_BIND_L][0] = HOST_PAD_BUTTON(9);   /* SDL's left shoulder */
    s->pad[HOST_BIND_R][0] = HOST_PAD_BUTTON(10);  /* SDL's right shoulder */
    s->pad[HOST_BIND_START][0] = HOST_PAD_BUTTON(6);
    s->pad[HOST_BIND_SELECT][0] = HOST_PAD_BUTTON(4);
}

static const struct {
    const char *name;
    int *value;
    int min;
    int max;
} sInts[] = {
    { "skip_intro", &gHostSettings.skipIntro, 0, 1 },
    { "language", &gHostSettings.language, 0, 5 },
    { "frame_rate", &gHostSettings.frameRate, 0, HOST_FPS_COUNT - 1 },
    { "scaling", &gHostSettings.scaling, 0, HOST_SCALING_COUNT - 1 },
    { "filter", &gHostSettings.filter, 0, 1 },
    { "colours", &gHostSettings.colors, 0, 1 },
    { "hud", &gHostSettings.hud, 0, HOST_HUD_COUNT - 1 },
    { "borders", &gHostSettings.borders, 0, HOST_BORDERS_COUNT - 1 },
    { "master", &gHostSettings.master, 0, 100 },
    { "music", &gHostSettings.music, 0, 100 },
    { "sfx", &gHostSettings.sfx, 0, 100 },
    { "audio_quality", &gHostSettings.audioQuality, 0, 1 },
    { "button_prompts", &gHostSettings.prompts, 0, HOST_PROMPTS_NINTENDO },
    { "auto_save", &gHostSettings.autoSave, 0, HOST_AUTOSAVE_COUNT - 1 },
    { "look_around", &gHostSettings.lookAround, 0, 1 },
    { "slide_jump_grace", &gHostSettings.slideGrace, 0, HOST_SLIDE_GRACE_COUNT - 1 },
    { "touch_size", &gHostSettings.touchSize, 0, 3 },
    { "touch_opacity", &gHostSettings.touchOpacity, 20, 100 },
    { "touch_in_menus", &gHostSettings.touchInMenus, 0, 1 },
    { "haptics", &gHostSettings.haptics, 0, 2 },
    { "text_size", &gHostSettings.textSize, 0, 1 },
};

static char sPath[1024];

static void Path(void)
{
    const char *dir = getenv("CRASHXS_DIR");

    if (sPath[0] == '\0') {
        snprintf(sPath, sizeof(sPath), "%s/settings.ini", dir != NULL && dir[0] ? dir : ".");
    }
}

void HostConfigSave(void)
{
    char tmp[1100];
    FILE *f;
    size_t i;

    Path();
    snprintf(tmp, sizeof(tmp), "%s.tmp", sPath);
    f = fopen(tmp, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# %s %s (Android) settings, written by the game.\n", HOST_PORT_NAME, HOST_PORT_VERSION);
    for (i = 0; i < sizeof(sInts) / sizeof(sInts[0]); i++) {
        fprintf(f, "%s=%d\n", sInts[i].name, *sInts[i].value);
    }
    if (fclose(f) == 0) {
        rename(tmp, sPath);
    }
    HostStreamSettingsChanged();
}

void HostConfigInit(void)
{
    char line[256];
    FILE *f;

    HostConfigDefaults(&gHostSettings);
    Path();
    f = fopen(sPath, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            char *eq = strchr(line, '=');
            size_t i;

            if (line[0] == '#' || eq == NULL) {
                continue;
            }
            *eq = '\0';
            for (i = 0; i < sizeof(sInts) / sizeof(sInts[0]); i++) {
                if (strcmp(line, sInts[i].name) == 0) {
                    int v = atoi(eq + 1);

                    *sInts[i].value = v < sInts[i].min ? sInts[i].min : v > sInts[i].max ? sInts[i].max : v;
                }
            }
        }
        fclose(f);
    }
    if (gHostSettings.frameRate != HOST_FPS_60 && gHostSettings.frameRate != HOST_FPS_120) {
        gHostSettings.frameRate = HOST_FPS_60;
    }
    HostFrontendApply(HOST_APPLY_AUDIO | HOST_APPLY_VIDEO);
}

const char *HostConfigDataDir(void)
{
    const char *dir = getenv("CRASHXS_DIR");

    return dir != NULL ? dir : ".";
}

int HostConfigSetPortable(int on, const char **why)
{
    (void)on;
    *why = "the app keeps everything in its own files.";
    return 0;
}

void HostFrontendApply(int what)
{
    (void)what;
    gHostGaxVolume[0] = (u32)gHostSettings.music * 256 / 100;
    gHostGaxVolume[1] = (u32)gHostSettings.sfx * 256 / 100;
    gHostAudioHq = gHostSettings.audioQuality != 0;
    HostStreamSettingsChanged();
}

int HostFrontendRefreshRate(void)
{
    return HostStreamDisplayHz();
}

/* 60, and 120 where the screen can show it. */
int HostFrontendFpsAllowed(int fps)
{
    return fps == 60 || (fps == 120 && HostStreamDisplayHz() >= 119);
}

void HostFrontendQuit(void)
{
}

#endif /* HOST_ANDROID */
