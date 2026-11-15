/* The PC port's settings (host_ui.h) and where its files live
 * (docs/platforms.md, "The settings menu").
 *
 * The files: settings.ini, the save (crashbandicootxs.sav) and the F12
 * screenshots (screenshots/). They live in the user's application data
 * folder (SDL's pref path: %APPDATA%\CrashBandicootXS on Windows,
 * ~/.local/share/CrashBandicootXS on Linux), or, in portable mode, next
 * to the executable: a settings.ini there that says portable=1 is portable
 * mode. Switching moves the files over (copied, then the old ones removed
 * once every copy is whole), so the save follows.
 *
 * v0.01 kept its save next to the executable with no settings.ini: a
 * save found there, with none in application data, is copied over once
 * (the original is left where it is). */

#ifdef HOST_SDL

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#include <SDL.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

#define SETTINGS_FILE "settings.ini"
#define SAVE_FILE     "crashbandicootxs.sav"
#define SHOTS_DIR     "screenshots"

struct HostSettings gHostSettings;

static char sExeDir[1024];
static char sAppDir[1024];
static char sDataDir[1024];
static char sSavePath[1100];
static int sReady;

void HostConfigDefaultKeys(struct HostSettings *s)
{
    memset(s->keys, 0, sizeof(s->keys));
    s->keys[HOST_BIND_UP][0] = SDL_SCANCODE_UP;
    s->keys[HOST_BIND_DOWN][0] = SDL_SCANCODE_DOWN;
    s->keys[HOST_BIND_LEFT][0] = SDL_SCANCODE_LEFT;
    s->keys[HOST_BIND_RIGHT][0] = SDL_SCANCODE_RIGHT;
    s->keys[HOST_BIND_A][0] = SDL_SCANCODE_X;
    s->keys[HOST_BIND_B][0] = SDL_SCANCODE_Z;
    s->keys[HOST_BIND_L][0] = SDL_SCANCODE_A;
    s->keys[HOST_BIND_R][0] = SDL_SCANCODE_S;
    s->keys[HOST_BIND_START][0] = SDL_SCANCODE_RETURN;
    s->keys[HOST_BIND_START][1] = SDL_SCANCODE_KP_ENTER;
    s->keys[HOST_BIND_SELECT][0] = SDL_SCANCODE_BACKSPACE;
    s->keys[HOST_BIND_SELECT][1] = SDL_SCANCODE_RSHIFT;
    s->keys[HOST_BIND_MENU][0] = SDL_SCANCODE_GRAVE;
    s->keys[HOST_BIND_FULLSCREEN][0] = SDL_SCANCODE_F11;
    s->keys[HOST_BIND_SCREENSHOT][0] = SDL_SCANCODE_F12;
    s->keys[HOST_BIND_FAST_FORWARD][0] = SDL_SCANCODE_TAB;
}

void HostConfigDefaultPad(struct HostSettings *s)
{
    memset(s->pad, 0, sizeof(s->pad));
    s->pad[HOST_BIND_UP][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP);
    s->pad[HOST_BIND_UP][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_LEFTY, 0);
    s->pad[HOST_BIND_DOWN][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    s->pad[HOST_BIND_DOWN][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_LEFTY, 1);
    s->pad[HOST_BIND_LEFT][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    s->pad[HOST_BIND_LEFT][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_LEFTX, 0);
    s->pad[HOST_BIND_RIGHT][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    s->pad[HOST_BIND_RIGHT][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_LEFTX, 1);
    /* The GBA's A and B by their place: the right face button and the
     * bottom one (B and A on an Xbox pad). */
    s->pad[HOST_BIND_A][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B);
    s->pad[HOST_BIND_B][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A);
    s->pad[HOST_BIND_L][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    s->pad[HOST_BIND_L][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    s->pad[HOST_BIND_R][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    s->pad[HOST_BIND_R][1] = HOST_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
    s->pad[HOST_BIND_START][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_START);
    s->pad[HOST_BIND_SELECT][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK);
    s->pad[HOST_BIND_MENU][0] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_GUIDE);
    s->pad[HOST_BIND_MENU][1] = HOST_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
}

void HostConfigDefaults(struct HostSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->pauseUnfocused = 1;
    s->windowScale = 3;
    s->frameRate = HOST_FPS_60;
    s->vsync = 1;
    s->scaling = HOST_SCALING_FIT;
    s->borders = HOST_BORDERS_AMBIENT;
    s->filter = HOST_FILTER_PIXELS;
    s->master = 100;
    s->music = 100;
    s->sfx = 100;
    s->deadzone = 35;
    HostConfigDefaultKeys(s);
    HostConfigDefaultPad(s);
}

/* --- settings.ini -------------------------------------------------------- */

static const struct {
    const char *name;
    int *value;
    int min;
    int max;
} sInts[] = {
    { "pause_unfocused", &gHostSettings.pauseUnfocused, 0, 1 },
    { "fullscreen", &gHostSettings.fullscreen, 0, 1 },
    { "window_scale", &gHostSettings.windowScale, 1, 8 },
    { "frame_rate_index", &gHostSettings.frameRate, 0, HOST_FPS_COUNT - 1 },
    { "vsync", &gHostSettings.vsync, 0, 1 },
    { "scaling", &gHostSettings.scaling, 0, HOST_SCALING_COUNT - 1 },
    { "borders", &gHostSettings.borders, 0, HOST_BORDERS_COUNT - 1 },
    { "filter", &gHostSettings.filter, 0, HOST_FILTER_COUNT - 1 },
    { "scanlines", &gHostSettings.scanlines, 0, 2 },
    { "lcd_grid", &gHostSettings.lcdGrid, 0, 1 },
    { "colors", &gHostSettings.colors, 0, 1 },
    { "ghosting", &gHostSettings.ghosting, 0, 1 },
    { "show_fps", &gHostSettings.showFps, 0, 1 },
    { "input_display", &gHostSettings.inputDisplay, 0, 2 },
    { "master_volume", &gHostSettings.master, 0, 100 },
    { "music_volume", &gHostSettings.music, 0, 100 },
    { "sfx_volume", &gHostSettings.sfx, 0, 100 },
    { "stick_deadzone", &gHostSettings.deadzone, 10, 90 },
};

static const char *const sBindKeys[HOST_BIND_COUNT] = {
    "up", "down", "left", "right", "a", "b", "l", "r", "start", "select",
    "settings_menu", "fullscreen", "screenshot", "fast_forward",
};

static void PadToText(int v, char *out, size_t size)
{
    if (HOST_PAD_IS_BUTTON(v)) {
        const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)(v & 0xFF));

        snprintf(out, size, "%s", s ? s : "");
    } else if (HOST_PAD_IS_AXIS(v)) {
        const char *s = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)((v & 0xFF) >> 1));

        snprintf(out, size, "%c%s", (v & 1) ? '+' : '-', s ? s : "");
    } else {
        out[0] = '\0';
    }
}

static int PadFromText(const char *s)
{
    if (*s == '+' || *s == '-') {
        SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(s + 1);

        return a == SDL_CONTROLLER_AXIS_INVALID ? 0 : HOST_PAD_AXIS(a, *s == '+');
    } else {
        SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(s);

        return b == SDL_CONTROLLER_BUTTON_INVALID ? 0 : HOST_PAD_BUTTON(b);
    }
}

static void Join(char *out, size_t size, const char *dir, const char *name)
{
    snprintf(out, size, "%s%s", dir, name);
}

static int WriteSettings(const char *dir, int portable)
{
    char path[1100];
    char tmp[1110];
    FILE *f;
    size_t i;
    int b;
    int s;

    Join(path, sizeof(path), dir, SETTINGS_FILE);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (f == NULL) {
        return 0;
    }
    fprintf(f, "# %s %s settings. Written by the game; the settings menu (~) changes them.\n", HOST_PORT_NAME,
            HOST_PORT_VERSION);
    fprintf(f, "portable=%d\n", portable);
    fprintf(f, "frame_rate=%d\n", gHostFpsValues[gHostSettings.frameRate]);
    for (i = 0; i < sizeof(sInts) / sizeof(sInts[0]); i++) {
        if (sInts[i].value == &gHostSettings.frameRate) {
            continue;
        }
        fprintf(f, "%s=%d\n", sInts[i].name, *sInts[i].value);
    }
    for (b = 0; b < HOST_BIND_COUNT; b++) {
        fprintf(f, "key_%s=", sBindKeys[b]);
        for (s = 0; s < HOST_BIND_SLOTS; s++) {
            const char *name = gHostSettings.keys[b][s] ? SDL_GetScancodeName((SDL_Scancode)gHostSettings.keys[b][s]) : "";

            fprintf(f, "%s%s", s ? "," : "", name);
        }
        fprintf(f, "\n");
    }
    for (b = 0; b < HOST_BIND_COUNT; b++) {
        fprintf(f, "pad_%s=", sBindKeys[b]);
        for (s = 0; s < HOST_BIND_SLOTS; s++) {
            char text[64];

            PadToText(gHostSettings.pad[b][s], text, sizeof(text));
            fprintf(f, "%s%s", s ? "," : "", text);
        }
        fprintf(f, "\n");
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return 0;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

/* Reads `dir`/settings.ini into gHostSettings; returns -1 if there is
 * none, else its portable= value. */
static int ReadSettings(const char *dir)
{
    char path[1100];
    char line[512];
    FILE *f;
    int portable = 0;

    Join(path, sizeof(path), dir, SETTINGS_FILE);
    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        char *value;
        size_t i;
        int b;

        if (line[0] == '#' || eq == NULL) {
            continue;
        }
        *eq = '\0';
        value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(line, "portable") == 0) {
            portable = atoi(value) != 0;
            continue;
        }
        if (strcmp(line, "frame_rate") == 0) {
            int fps = atoi(value);

            for (b = 0; b < HOST_FPS_COUNT; b++) {
                if (gHostFpsValues[b] == fps) {
                    gHostSettings.frameRate = b;
                }
            }
            continue;
        }
        for (i = 0; i < sizeof(sInts) / sizeof(sInts[0]); i++) {
            if (strcmp(line, sInts[i].name) == 0) {
                int v = atoi(value);

                if (v < sInts[i].min) {
                    v = sInts[i].min;
                }
                if (v > sInts[i].max) {
                    v = sInts[i].max;
                }
                *sInts[i].value = v;
            }
        }
        for (b = 0; b < HOST_BIND_COUNT; b++) {
            int isKey = strncmp(line, "key_", 4) == 0 && strcmp(line + 4, sBindKeys[b]) == 0;
            int isPad = strncmp(line, "pad_", 4) == 0 && strcmp(line + 4, sBindKeys[b]) == 0;
            char *p = value;
            int s;

            if (!isKey && !isPad) {
                continue;
            }
            for (s = 0; s < HOST_BIND_SLOTS; s++) {
                char *comma = strchr(p, ',');
                int v;

                if (comma != NULL) {
                    *comma = '\0';
                }
                if (*p == '\0') {
                    v = 0;
                } else if (isKey) {
                    v = SDL_GetScancodeFromName(p);
                } else {
                    v = PadFromText(p);
                }
                (isKey ? gHostSettings.keys : gHostSettings.pad)[b][s] = v;
                if (comma == NULL) {
                    for (s++; s < HOST_BIND_SLOTS; s++) {
                        (isKey ? gHostSettings.keys : gHostSettings.pad)[b][s] = 0;
                    }
                    break;
                }
                p = comma + 1;
            }
        }
    }
    fclose(f);
    return portable;
}

/* --- The files ----------------------------------------------------------- */

static int Exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static int CopyFile(const char *from, const char *to)
{
    char tmp[1200];
    char buf[65536];
    FILE *in;
    FILE *out;
    size_t n;
    int ok = 1;

    in = fopen(from, "rb");
    if (in == NULL) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", to);
    out = fopen(tmp, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) {
        ok = 0;
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = 0;
    }
    if (ok) {
        remove(to);
        ok = rename(tmp, to) == 0;
    }
    if (!ok) {
        remove(tmp);
    }
    return ok;
}

/* The files a data folder holds, relative to it: the save and the
 * screenshots (settings.ini is written, not copied). */
static int ListFiles(const char *dir, char names[][128], int max)
{
    char path[1100];
    int n = 0;
    DIR *d;
    struct dirent *e;

    Join(path, sizeof(path), dir, SAVE_FILE);
    if (Exists(path)) {
        snprintf(names[n++], 128, "%s", SAVE_FILE);
    }
    Join(path, sizeof(path), dir, SHOTS_DIR);
    d = opendir(path);
    if (d != NULL) {
        while ((e = readdir(d)) != NULL && n < max) {
            size_t len = strlen(e->d_name);

            if (len > 4 && strcmp(e->d_name + len - 4, ".png") == 0) {
                snprintf(names[n++], 128, "%s/%s", SHOTS_DIR, e->d_name);
            }
        }
        closedir(d);
    }
    return n;
}

static void PointSaveAt(const char *dir)
{
    snprintf(sDataDir, sizeof(sDataDir), "%s", dir);
    Join(sSavePath, sizeof(sSavePath), dir, SAVE_FILE);
    gHostOptions.save = sSavePath;
}

const char *HostConfigDataDir(void)
{
    return sDataDir;
}

void HostConfigInit(void)
{
    char *base = SDL_GetBasePath();
    char *pref = SDL_GetPrefPath("", "CrashBandicootXS");
    const char *explicitSave = gHostOptions.save;
    int portable;

    snprintf(sExeDir, sizeof(sExeDir), "%s", base ? base : "./");
    snprintf(sAppDir, sizeof(sAppDir), "%s", pref ? pref : sExeDir);
    SDL_free(base);
    SDL_free(pref);
    HostConfigDefaults(&gHostSettings);

    portable = ReadSettings(sExeDir);
    if (portable == 1) {
        gHostSettings.portable = 1;
        PointSaveAt(sExeDir);
    } else {
        char oldSave[1100];
        char newSave[1100];

        HostConfigDefaults(&gHostSettings);
        ReadSettings(sAppDir);
        gHostSettings.portable = 0;
        PointSaveAt(sAppDir);
        /* v0.01's save, next to the executable. */
        Join(oldSave, sizeof(oldSave), sExeDir, SAVE_FILE);
        Join(newSave, sizeof(newSave), sAppDir, SAVE_FILE);
        if (portable == -1 && Exists(oldSave) && !Exists(newSave)) {
            CopyFile(oldSave, newSave);
        }
    }
    if (explicitSave != NULL) {
        gHostOptions.save = explicitSave; /* --save wins */
    }
    sReady = 1;
}

void HostConfigSave(void)
{
    if (sReady) {
        WriteSettings(sDataDir, gHostSettings.portable);
    }
}

int HostConfigSetPortable(int on, const char **why)
{
    static char names[512][128];
    const char *from = on ? sAppDir : sExeDir;
    const char *to = on ? sExeDir : sAppDir;
    char shots[1100];
    int n;
    int i;

    if (on == gHostSettings.portable) {
        return 1;
    }
    if (strcmp(sAppDir, sExeDir) == 0) {
        *why = "the game's folder is its appdata folder here.";
        return 0;
    }
    HostSaveFlush();
    Join(shots, sizeof(shots), to, SHOTS_DIR);
    n = ListFiles(from, names, 512);
    if (n > 1) {
        MKDIR(shots);
    }
    /* The settings first: a folder that can't take them can't take the
     * rest (the game's folder in Program Files, say). */
    if (!WriteSettings(to, on)) {
        *why = on ? "can't write to the game's folder: move the game somewhere you can write to."
                  : "can't write to the appdata folder.";
        return 0;
    }
    for (i = 0; i < n; i++) {
        char a[1300];
        char b[1300];

        snprintf(a, sizeof(a), "%s%s", from, names[i]);
        snprintf(b, sizeof(b), "%s%s", to, names[i]);
        if (!CopyFile(a, b)) {
            char settings[1100];

            /* Back as it was: the new folder's settings.ini would claim
             * the files. */
            Join(settings, sizeof(settings), to, SETTINGS_FILE);
            if (on) {
                remove(settings);
            } else {
                WriteSettings(to, 0);
            }
            *why = "copying the files failed: nothing was changed.";
            return 0;
        }
    }
    /* Every copy is whole: the old ones go. */
    for (i = 0; i < n; i++) {
        char a[1300];

        snprintf(a, sizeof(a), "%s%s", from, names[i]);
        remove(a);
    }
    {
        char old[1100];

        Join(old, sizeof(old), from, SHOTS_DIR);
        rmdir(old); /* only if empty */
        Join(old, sizeof(old), from, SETTINGS_FILE);
        if (on) {
            WriteSettings(from, 0); /* appdata's: not portable, nothing else */
        } else {
            remove(old);
        }
    }
    gHostSettings.portable = on;
    PointSaveAt(to);
    return 1;
}

#endif /* HOST_SDL */
