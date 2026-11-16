#ifndef GUARD_PC_HOST_UI_H
#define GUARD_PC_HOST_UI_H

/* The PC port's own settings (config.c), its settings menu (ui.c) and what
 * they share with the window (sdl.c). docs/platforms.md, "The settings
 * menu". None of it touches the game: the menu runs while the game waits
 * in VBlankIntrWait, and the settings change only how the frames are shown
 * and heard and how the keys are read. */

#include "gba/types.h"

/* The frame rates the window can show. 60 is the GBA's own (59.73 Hz):
 * every frame is the game's; the others add frames in between, the
 * game's last two frames interpolated (ppu_hd.c, interp.c). */
enum {
    HOST_FPS_60,
    HOST_FPS_90,
    HOST_FPS_120,
    HOST_FPS_144,
    HOST_FPS_240,
    HOST_FPS_COUNT
};
extern const int gHostFpsValues[HOST_FPS_COUNT];

enum {
    HOST_FILTER_PIXELS,
    HOST_FILTER_SMOOTH,
    HOST_FILTER_SHARP,
    HOST_FILTER_SCALE2X,
    HOST_FILTER_SCALE3X,
    HOST_FILTER_CARTOON,      /* xBRZ up to the window's size, then smooth */
    HOST_FILTER_CARTOON_SOFT, /* xBRZ 2x, then smooth: softer */
    HOST_FILTER_COUNT
};

enum {
    HOST_BORDERS_BLACK,
    HOST_BORDERS_AMBIENT, /* the picture's own colours, blurred */
    HOST_BORDERS_THEMED,  /* the world's art (borders.c) */
    HOST_BORDERS_COUNT
};

enum {
    HOST_SCALING_FIT,
    HOST_SCALING_INTEGER,
    HOST_SCALING_STRETCH,
    HOST_SCALING_COUNT
};

/* What a binding does: the GBA's ten buttons, then the port's own keys. */
enum {
    HOST_BIND_UP,
    HOST_BIND_DOWN,
    HOST_BIND_LEFT,
    HOST_BIND_RIGHT,
    HOST_BIND_A,
    HOST_BIND_B,
    HOST_BIND_L,
    HOST_BIND_R,
    HOST_BIND_START,
    HOST_BIND_SELECT,
    HOST_BIND_GBA_COUNT,
    HOST_BIND_MENU = HOST_BIND_GBA_COUNT,
    HOST_BIND_FULLSCREEN,
    HOST_BIND_SCREENSHOT,
    HOST_BIND_FAST_FORWARD,
    HOST_BIND_COUNT
};
#define HOST_BIND_SLOTS 2

/* A controller binding: 0 none, a button, or an axis pushed one way. */
#define HOST_PAD_NONE        0
#define HOST_PAD_BUTTON(b)   (0x100 | (b))
#define HOST_PAD_AXIS(a, up) (0x200 | (a) << 1 | ((up) ? 1 : 0))
#define HOST_PAD_IS_BUTTON(v) (((v) & 0xF00) == 0x100)
#define HOST_PAD_IS_AXIS(v)   (((v) & 0xF00) == 0x200)

struct HostSettings {
    /* General */
    int portable;
    int skipIntro; /* boot straight to the title (intro_skip.c) */
    int language;  /* the language select's last pick, for skipped boots */
    int pauseUnfocused;
    /* Display */
    int fullscreen;
    int windowScale; /* 1-8 */
    int frameRate;   /* HOST_FPS_* */
    int vsync;
    int scaling; /* HOST_SCALING_* */
    int borders; /* HOST_BORDERS_*: what fills the window around the picture */
    /* Graphics */
    int filter;    /* HOST_FILTER_* */
    int scanlines; /* 0 off, 1 light, 2 dark */
    int lcdGrid;
    int colors; /* 0 original, 1 the GBA's screen */
    int ghosting;
    /* Interface */
    int showFps;
    int inputDisplay; /* 0 off, 1 bottom left, 2 bottom right */
    int hud;          /* HOST_HUD_* */
    int speedrunTimer; /* HOST_TIMER_* */
    /* Audio, percent */
    int master;
    int music;
    int sfx;
    int audioQuality; /* 0 original (the GBA's), 1 high (gax_dsp.c, sdl.c) */
    /* Controls */
    int deadzone; /* percent */
    int rumble;   /* 0 off, 1 light, 2 strong (rumble.c) */
    int keys[HOST_BIND_COUNT][HOST_BIND_SLOTS]; /* SDL scancodes, 0 none */
    int pad[HOST_BIND_COUNT][HOST_BIND_SLOTS];  /* HOST_PAD_* */
    /* Gameplay */
    int autoSave; /* HOST_AUTOSAVE_* */
    /* Extras: Discord Rich Presence (discord.c) */
    int discord;
    int discordProgress;
    char discordAppId[32]; /* the Discord application's id, digits */
};
extern struct HostSettings gHostSettings;

/* The Discord application Rich Presence talks as (its name is what the
 * profile says is being played; its art assets are the pictures), unless
 * settings.ini's discord_app_id names another (empty there: this one). */
#ifndef HOST_DISCORD_APP_ID
#define HOST_DISCORD_APP_ID "1571726745764110526"
#endif

/* hud_hook.cpp: Interface's HUD, and the HUD's numbers for the left
 * border (borders.c). */
enum {
    HOST_HUD_ON,
    HOST_HUD_OFF,
    HOST_HUD_BORDERS,
    HOST_HUD_COUNT
};
struct HostHud {
    u32 frame; /* HostFrame() of its last update: in a level when recent */
    s32 lives;
    s32 wumpa;
    s32 crates;
    s32 crateTotal;
    s32 timeTrial;
    s32 minutes;
    s32 seconds;
    s32 tenths;
    s32 boss;
};
extern struct HostHud gHostHud;

/* timer.c: Interface's Speedrun timer. */
enum {
    HOST_TIMER_OFF,
    HOST_TIMER_LEVEL, /* the level's time and its split */
    HOST_TIMER_RUN,   /* and the run's */
    HOST_TIMER_COUNT
};
void HostTimerVBlank(void);
void HostTimerRunStart(void);
void HostTimerWarpRoom(void);
int HostTimerLines(char big[32], char line2[64], char line3[64]);

/* intro_skip.c: General's Skip intro, as this launch has it. */
int HostSkipIntro(void);

/* autosave.c: Gameplay's Auto save. */
enum {
    HOST_AUTOSAVE_OFF,
    HOST_AUTOSAVE_IN_USE, /* the slot loaded from or last saved to */
    HOST_AUTOSAVE_SLOT4,  /* always the fourth */
    HOST_AUTOSAVE_COUNT
};
extern int gHostAutosaveForce; /* --auto-save; -1 none */
void HostAutosaveSessionStart(void);
void HostAutosaveLoaded(int row); /* -1: not from a cartridge slot */
void HostAutosaveSaved(int row);
int HostAutosaveSlotInUse(void);
void HostAutosaveSlotGone(void);
void HostAutosaveWarpRoom(void);
int HostAutosaveWrite(int row); /* autosave_hooks.cpp */
void HostSaveBackup(void);      /* eeprom.c: the save file to <save>.bak */
void HostToast(const char *text); /* sdl.c: a few seconds in a corner */

/* discord.c: Rich Presence. The status, for the settings menu. */
enum {
    HOST_DISCORD_OFF,
    HOST_DISCORD_NO_ID,
    HOST_DISCORD_NOT_RUNNING,
    HOST_DISCORD_CONNECTED,
    HOST_DISCORD_BAD_ID,
    HOST_DISCORD_STATUS_COUNT
};
extern int gHostDiscordStatus;
extern s32 gHostInWarpRoom;
void HostDiscordInit(void);
void HostDiscordQuit(void);
void HostDiscordFrame(void);
void HostBordersWriteDiscordAssets(const char *dir); /* borders.c */

/* config.c */
void HostConfigDefaults(struct HostSettings *s);
void HostConfigDefaultKeys(struct HostSettings *s);
void HostConfigDefaultPad(struct HostSettings *s);
/* Finds the data folder (the executable's in portable mode, else the
 * user's application data folder), reads settings.ini, points the save at
 * it. */
void HostConfigInit(void);
void HostConfigSave(void);
const char *HostConfigDataDir(void);
/* Moves the files to the other folder; 0 and a reason if it can't. */
int HostConfigSetPortable(int on, const char **why);

/* What changed, for HostFrontendApply (sdl.c). */
enum {
    HOST_APPLY_WINDOW = 1,
    HOST_APPLY_VSYNC = 2,
    HOST_APPLY_AUDIO = 4,
    HOST_APPLY_VIDEO = 8,
};
void HostFrontendApply(int what);
/* The display's refresh rate (0 unknown), and whether a frame rate can be
 * shown on it. */
int HostFrontendRefreshRate(void);
int HostFrontendFpsAllowed(int fps);
void HostFrontendQuit(void);

/* ui.c: the settings menu, drawn on a 240x160 canvas in the game's own
 * style (its fonts and its pause screen's art). */
#define HOST_UI_WIDTH  240
#define HOST_UI_HEIGHT 160

enum {
    HOST_UI_UP,
    HOST_UI_DOWN,
    HOST_UI_LEFT,
    HOST_UI_RIGHT,
    HOST_UI_OK,
    HOST_UI_BACK,
    HOST_UI_CLEAR, /* a binding: none */
};

void HostUiOpen(void);
int HostUiIsOpen(void);
void HostUiInput(int action);
/* While a binding waits for its key or button: which (0 none, 1 a key,
 * 2 a controller's), and the answer. */
int HostUiCapturing(void);
void HostUiCaptured(int value);
void HostUiCaptureCancel(void);
void HostUiDraw(u32 *canvas);
/* The names the bindings show (sdl.c knows SDL's). */
extern const char *(*gHostUiKeyName)(int scancode);
extern const char *(*gHostUiPadName)(int binding);

/* The game's fonts, for the window's overlays (the FPS, the inputs). */
enum {
    HOST_UI_PAL_NORMAL,
    HOST_UI_PAL_WHITE,
    HOST_UI_PAL_GREY,
    HOST_UI_PAL_GREY_LIGHT,
    HOST_UI_PAL_COUNT
};
int HostUiTextWidth(int large, const char *s);
/* Draws `s` with its cursor at (x, y) (the game's posY: the small font's
 * glyphs reach 8 pixels above it) on a canvas `w` x `h` of XRGB pixels;
 * with `alpha`, the pixels it draws get alpha 0xFF (ARGB canvases). */
void HostUiText(u32 *canvas, int w, int h, int large, int x, int y, const char *s, int pal);
/* --ui-preview DIR: each page of the menu as a PNG (headless). */
void HostUiPreview(const char *dir);

#endif /* GUARD_PC_HOST_UI_H */
