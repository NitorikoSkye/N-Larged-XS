/* The PC settings menu (docs/platforms.md, "The settings menu"): opened
 * with ~ (or the controller's Guide button), it lets the player change
 * the port's own settings (host_ui.h, config.c) while the game waits.
 *
 * It is drawn in the game's own style, from the game's own pieces
 * (ui_assets.c, made by tools/ui_assets.py from the fonts and the pause
 * screen's art), the way the pause menu draws itself (pause_menu_draw.cpp):
 * the large font for the title at y 14, the small font for the rows, the
 * selected row in white (the pause menu's palette 15), the values as
 * " <75%>". The first page is the pause screen itself, the categories in
 * its left box and a word about the highlighted one on its monitor; a
 * category's page widens the pause screen's box across the screen (its
 * slats re-pitched to the rows) and puts the highlighted option's help in
 * the band under the title. Settings that can't be used now are grey. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

extern const unsigned char gUiSmallFontCells[79][256];
extern const signed char gUiSmallFontAdvance[79];
extern const signed char gUiSmallFontYOffset[79];
extern const unsigned char gUiSmallFontLookup[256];
extern const unsigned char gUiLargeFontCells[75][256];
extern const signed char gUiLargeFontAdvance[75];
extern const signed char gUiLargeFontYOffset[75];
extern const unsigned char gUiLargeFontLookup[256];
extern const unsigned short gUiFontPalette[16];
extern const unsigned char gUiPauseArt[160][240];
extern const unsigned short gUiPausePalette[256];

/* The menu's own layout is 240 wide; widescreen (HOST_WIDE) puts it in
 * the middle of its wider canvas (HostUiDraw). */
#define W 240
#define H HOST_UI_HEIGHT

const int gHostFpsValues[HOST_FPS_COUNT] = { 60, 90, 120, 144, 240 };

static const char *DefaultKeyName(int scancode)
{
    static char text[16];

    snprintf(text, sizeof(text), "key %d", scancode);
    return text;
}

static const char *DefaultPadName(int binding)
{
    static char text[16];

    snprintf(text, sizeof(text), "pad %d", binding & 0xFF);
    return text;
}

const char *(*gHostUiKeyName)(int scancode) = DefaultKeyName;
const char *(*gHostUiPadName)(int binding) = DefaultPadName;

/* --- Drawing ------------------------------------------------------------ */

static inline u32 Color15(u32 c)
{
    u32 r = c & 0x1F;
    u32 g = (c >> 5) & 0x1F;
    u32 b = (c >> 10) & 0x1F;

    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return r << 16 | g << 8 | b;
}

/* The font's palettes: the game's (normal), its palette 15 (white, the
 * selected row: the outline and the shadow kept, the rest white), and
 * greys for what can't be picked. */
static u32 sFontPal[HOST_UI_PAL_COUNT][16];
static u32 sArtPal[256];
static int sPalReady;

static void InitPalettes(void)
{
    s32 i;

    if (sPalReady) {
        return;
    }
    for (i = 0; i < 16; i++) {
        u32 c = Color15(gUiFontPalette[i]);
        u32 r = (c >> 16) & 0xFF;
        u32 g = (c >> 8) & 0xFF;
        u32 b = c & 0xFF;
        u32 l = (r * 30 + g * 59 + b * 11) / 100;
        u32 dark = 40 + l * 45 / 100;
        u32 light = 110 + l * 55 / 100;

        sFontPal[HOST_UI_PAL_NORMAL][i] = c;
        sFontPal[HOST_UI_PAL_WHITE][i] = (i == 1 || i == 6) ? c : 0xFFFFFF;
        sFontPal[HOST_UI_PAL_GREY][i] = (i == 1 || i == 6) ? c : dark << 16 | dark << 8 | dark;
        sFontPal[HOST_UI_PAL_GREY_LIGHT][i] =
            (i == 1 || i == 6) ? c : light << 16 | light << 8 | light;
    }
    for (i = 0; i < 256; i++) {
        sArtPal[i] = Color15(gUiPausePalette[i]);
    }
    sPalReady = 1;
}

struct Font {
    const unsigned char (*cells)[256];
    const signed char *advance;
    const signed char *yOffset;
    const unsigned char *lookup;
    int space;
};

static const struct Font sFonts[2] = {
    { gUiSmallFontCells, gUiSmallFontAdvance, gUiSmallFontYOffset, gUiSmallFontLookup, 4 },
    { gUiLargeFontCells, gUiLargeFontAdvance, gUiLargeFontYOffset, gUiLargeFontLookup, 6 },
};

int HostUiTextWidth(int large, const char *s)
{
    const struct Font *f = &sFonts[large != 0];
    int w = 0;

    for (; *s != '\0'; s++) {
        u8 c = (u8)*s;
        u8 g = f->lookup[c];

        if (c == ' ' || g == 255) {
            w += f->space;
        } else {
            w += f->advance[g];
        }
    }
    return w;
}

void HostUiText(u32 *canvas, int w, int h, int large, int x, int y, const char *s, int pal)
{
    const struct Font *f = &sFonts[large != 0];
    const u32 *colors;

    InitPalettes();
    colors = sFontPal[pal];
    for (; *s != '\0'; s++) {
        u8 c = (u8)*s;
        u8 g = f->lookup[c];
        int top;
        int cy;

        if (c == ' ' || g == 255) {
            x += f->space;
            continue;
        }
        /* The button icons (A, B, L, R) keep their colours: white would
         * wipe out their letters. */
        colors = sFontPal[(!large && pal == HOST_UI_PAL_WHITE && c >= 'A' && c <= 'Z')
                              ? HOST_UI_PAL_NORMAL
                              : pal];
        /* The game draws a glyph's OBJ at posY + yOffset (font_glyph.cpp). */
        top = y + f->yOffset[g];
        for (cy = 0; cy < 16; cy++) {
            int py = top + cy;
            int cx;

            if (py < 0 || py >= h) {
                continue;
            }
            for (cx = 0; cx < 16; cx++) {
                int px = x + cx;
                u8 index = f->cells[g][cy * 16 + cx];

                if (index != 0 && px >= 0 && px < w) {
                    canvas[py * w + px] = 0xFF000000 | colors[index];
                }
            }
        }
        x += f->advance[g];
    }
}

static u32 *sCanvas;

static void Text(int large, int x, int y, const char *s, int pal)
{
    HostUiText(sCanvas, W, H, large, x, y, s, pal);
}

static void TextCentered(int large, int cx, int y, const char *s, int pal)
{
    Text(large, cx - HostUiTextWidth(large, s) / 2, y, s, pal);
}

static void TextRight(int large, int right, int y, const char *s, int pal)
{
    Text(large, right - HostUiTextWidth(large, s), y, s, pal);
}


/* The pause art's pixel (x, y). */
static inline u32 Art(int x, int y)
{
    return 0xFF000000 | sArtPal[gUiPauseArt[y][x]];
}

static void DrawPauseScreen(void)
{
    int x;
    int y;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            sCanvas[y * W + x] = Art(x, y);
        }
    }
}

/* The pause screen's left box, widened to the screen (x 0-240) with its
 * slats re-pitched to ROW_PITCH: a column of the canvas is a column of the
 * art (its left plate and the box's left end, the slats' middle repeated,
 * the box's right end and the plate after it); a box row is a row of a
 * slat (the art's slat compressed: its top edge and highlight, body, and
 * shadow). The two bands at the top are the title band twice. */
/* The rows of the options and bindings pages: 7 rows 12 apart, or with
 * large text 4 rows 20 apart (Layout). */
static int sRowPitch = 12;
static int sRowCount = 7;
#define ROW_PITCH  sRowPitch
#define ROWS_TOP   64
#define ROW_COUNT  sRowCount
static const u8 sSlatRows[12] = { 68, 69, 70, 73, 76, 76, 76, 79, 82, 83, 84, 86 };

static int Large(void)
{
    return gHostSettings.textSize != 0;
}

/* The rows' text from the edges: less with large text, which is the
 * title's font (it has no < and >: the values' arrows are drawn). */
#define ROW_MARGIN (Large() ? 14 : 18)
#define ROW_FONT   (Large() ? 1 : 0)
#define ARROW_W    6

static void Layout(void)
{
    sRowPitch = Large() ? 20 : 12;
    sRowCount = Large() ? 4 : 7;
}

static int ArtColumn(int x, int *plate)
{
    *plate = 0;
    if (x < 9) {
        *plate = 1;
        return x;
    }
    if (x < 21) {
        return x;
    }
    if (x < W - 21) {
        return 50;
    }
    if (x < W - 9) {
        return x - (W - 21) + 80;
    }
    *plate = 1;
    return x - (W - 9) + 92;
}

static void DrawWidePanel(void)
{
    int x;
    int y;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int plate;
            int ax = ArtColumn(x, &plate);
            int ay;

            if (y < 32) {
                ay = y;
                ax = x;
            } else if (y < 56) {
                ay = y - 24; /* the title band again */
                ax = x;
            } else if (y < 64) {
                ay = y - 56; /* the bolts along the top */
                ax = x;
            } else if (y < ROWS_TOP + ROW_PITCH * ROW_COUNT) {
                ay = plate ? y : sSlatRows[(y - ROWS_TOP) % ROW_PITCH * 12 / ROW_PITCH];
            } else if (y < 151) {
                ay = plate ? y : 147 + (y - (ROWS_TOP + ROW_PITCH * ROW_COUNT));
                if (ay > 150) {
                    ay = 150;
                }
            } else {
                ay = y;
                if (!plate) {
                    ax = x; /* the bottom plate as it is */
                }
            }
            sCanvas[y * W + x] = Art(ax, ay);
        }
    }
}

/* A small triangle in the font's colours: more rows above or below. */
static void Arrow(int cx, int y, int down)
{
    const u32 *pal = sFontPal[HOST_UI_PAL_NORMAL];
    int r;

    for (r = 0; r < 4; r++) {
        int half = down ? 3 - r : r;
        int x;

        for (x = -half - 1; x <= half + 1; x++) {
            int edge = x == -half - 1 || x == half + 1;
            sCanvas[(y + r) * W + cx + x] = 0xFF000000 | (edge ? pal[6] : pal[r < 2 ? 4 : 8]);
        }
    }
}

static void Dim(int x0, int y0, int x1, int y1)
{
    int x;
    int y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            u32 c = sCanvas[y * W + x];

            sCanvas[y * W + x] = 0xFF000000 | ((c >> 2) & 0x3F3F3F);
        }
    }
}

/* Splits `s` into lines at most `width` pixels wide; returns the count. */
static int Wrap(const char *s, int width, char lines[][64], int maxLines)
{
    int n = 0;

    while (*s != '\0' && n < maxLines) {
        char line[64] = "";
        int len = 0;

        while (*s == ' ') {
            s++;
        }
        while (*s != '\0') {
            char word[64];
            int wl = 0;
            char test[128];

            while (s[wl] != '\0' && s[wl] != ' ' && wl < 63) {
                word[wl] = s[wl];
                wl++;
            }
            word[wl] = '\0';
            snprintf(test, sizeof(test), "%s%s%s", line, len ? " " : "", word);
            if (len != 0 && HostUiTextWidth(0, test) > width) {
                break;
            }
            snprintf(line, sizeof(line), "%s", test);
            len = (int)strlen(line);
            s += wl;
            while (*s == ' ') {
                s++;
            }
        }
        snprintf(lines[n++], 64, "%s", line);
    }
    return n;
}

/* --- The menu's contents ------------------------------------------------- */

enum {
    OPT_CHOICE,  /* <value> from a list */
    OPT_PERCENT, /* <75%> in steps of 5 */
    OPT_ACTION,  /* A does something */
};

struct Option {
    const char *label;
    const char *help;
    int type;
    int *value;
    const char *const *choices;
    int count;
    int apply;                    /* HOST_APPLY_* when it changes */
    int (*available)(int choice); /* a choice that can't be picked now: grey, skipped */
    int (*enabled)(void);         /* the row can't be used now: grey */
    void (*action)(void);
    int min; /* OPT_PERCENT */
    int max;
};

struct Category {
    const char *name;
    const char *about;
    const struct Option *options;
    int count;
};

static const char *const sOffOn[] = { "off", "on" };
static const char *const sWindowModes[] = { "windowed", "fullscreen" };
/* The window size is 1-8 times the GBA's: the list's index. */
static const char *const sScales[] = { "", "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x" };
static const char *const sFpsNames[] = { "60 fps", "90 fps", "120 fps", "144 fps", "240 fps" };
static const char *const sScalings[] = { "fit", "integer", "stretch" };
static const char *const sBorders[] = { "black", "ambient", "themed" };
static const char *const sFilters[] = { "pixels",  "smooth",  "sharp smooth", "scale2x",
                                        "scale3x", "cartoon", "cartoon soft" };
static const char *const sScanlines[] = { "off", "light", "dark" };
static const char *const sColorModes[] = { "original", "gba screen" };
static const char *const sCorners[] = { "off", "left", "right" };

static int sCategory;
static int sCursor[8];
static int sTop;
static int sPage;
static int sOpen;
static int sBindPad; /* the bindings page: 0 keyboard, 1 controller */
static int sBindRow;
static int sBindSlot;
static int sCapturing;
static int sConfirm; /* a question on top of the page */
static void (*sConfirmYes)(void);
static const char *sConfirmText;
static char sMessage[96];
static int sMessageTime;

enum {
    PAGE_CATEGORIES,
    PAGE_OPTIONS,
    PAGE_BINDINGS,
};

static void Message(const char *s)
{
    snprintf(sMessage, sizeof(sMessage), "%s", s);
    sMessageTime = 240;
}

static int ScaleAvailable(int choice)
{
    return choice >= 1;
}

static int WindowedOnly(void)
{
    return !gHostSettings.fullscreen;
}

static int FpsAvailable(int choice)
{
    return HostFrontendFpsAllowed(gHostFpsValues[choice]);
}


static int NotStretched(void)
{
    /* widescreen: no borders (HOST_BORDERS_SHOWN) */
    return !HOST_WIDE && gHostSettings.scaling != HOST_SCALING_STRETCH;
}

/* widescreen has no border for the HUD (HOST_BORDERS_SHOWN) */
static int HudAvailable(int choice)
{
    return !(HOST_WIDE && choice == HOST_HUD_BORDERS);
}

static int Never(void)
{
    return 0;
}

static void TogglePortable(void)
{
    const char *why = NULL;
    int want = !gHostSettings.portable;

    if (HostConfigSetPortable(want, &why)) {
        Message(want ? "portable mode on: settings and saves are now next to the game."
                     : "portable mode off: settings and saves are now in appdata.");
    } else {
        Message(why != NULL ? why : "that didn't work.");
    }
}

static void SwitchWide(void)
{
    HostConfigSave();
    HostWideSwitch();
}

static void AskWide(void)
{
    sConfirm = 1;
    sConfirmText =
        gHostSettings.widescreen ? "restart, widescreen off?" : "restart, widescreen on?";
    sConfirmYes = SwitchWide;
}

static void Quit(void)
{
    HostFrontendQuit();
}

static void AskQuit(void)
{
    sConfirm = 1;
    sConfirmText = "quit the game?";
    sConfirmYes = Quit;
}

static void OpenKeyboard(void)
{
    sPage = PAGE_BINDINGS;
    sBindPad = 0;
    sBindRow = 0;
    sBindSlot = 0;
    sTop = 0;
}

static void OpenController(void)
{
    sPage = PAGE_BINDINGS;
    sBindPad = 1;
    sBindRow = 0;
    sBindSlot = 0;
    sTop = 0;
}

static const struct Option sGeneral[] = {
    { "portable mode", "settings and saves next to the game, not in appdata.", OPT_ACTION,
      &gHostSettings.portable, sOffOn, 2, 0, NULL, NULL, TogglePortable },
    { "pause when unfocused", "the game stops while its window isn't the active one.", OPT_CHOICE,
      &gHostSettings.pauseUnfocused, sOffOn, 2, 0 },
    { "skip intro", "from the next launch: straight to the title, no logos or intro.", OPT_CHOICE,
      &gHostSettings.skipIntro, sOffOn, 2, 0 },
    { "quit game", "close the game. your progress is in its save, as the game saves it.",
      OPT_ACTION, NULL, NULL, 0, 0, NULL, NULL, AskQuit },
};

static const struct Option sDisplay[] = {
    { "window mode", "a window, or the whole screen (borderless).", OPT_CHOICE,
      &gHostSettings.fullscreen, sWindowModes, 2, HOST_APPLY_WINDOW },
    { "window size", "the window's size, in times the gba's 240x160 screen.", OPT_CHOICE,
      &gHostSettings.windowScale, sScales, 9, HOST_APPLY_WINDOW, ScaleAvailable, WindowedOnly },
    { "frame rate", "extra frames between the game's own. same game speed.", OPT_CHOICE,
      &gHostSettings.frameRate, sFpsNames, HOST_FPS_COUNT, HOST_APPLY_VIDEO, FpsAvailable },
    { "vsync", "wait for the display before each frame: no tearing.", OPT_CHOICE,
      &gHostSettings.vsync, sOffOn, 2, HOST_APPLY_VSYNC },
    { "scaling", "fit keeps the shape, integer keeps pixels even.", OPT_CHOICE,
      &gHostSettings.scaling, sScalings, HOST_SCALING_COUNT, HOST_APPLY_VIDEO },
    { "borders", "around the picture: black, its colours blurred, or the world's art.", OPT_CHOICE,
      &gHostSettings.borders, sBorders, HOST_BORDERS_COUNT, HOST_APPLY_VIDEO, NULL, NotStretched },
    { "widescreen", "a wider 288x160 picture. restarts the game: unsaved progress is lost.",
      OPT_ACTION, &gHostSettings.widescreen, sOffOn, 2, 0, NULL, HostWideAvailable, AskWide },
};

static const struct Option sGraphics[] = {
    { "filter", "how the pixels are scaled up. cartoon smooths the outlines.", OPT_CHOICE,
      &gHostSettings.filter, sFilters, HOST_FILTER_COUNT, HOST_APPLY_VIDEO },
    { "scanlines", "dark lines between the rows of pixels, like a crt.", OPT_CHOICE,
      &gHostSettings.scanlines, sScanlines, 3, HOST_APPLY_VIDEO },
    { "lcd grid", "the gaps between the pixels of the gba's screen.", OPT_CHOICE,
      &gHostSettings.lcdGrid, sOffOn, 2, HOST_APPLY_VIDEO },
    { "colours", "the game's own colours, or as the gba's darker screen shows them.", OPT_CHOICE,
      &gHostSettings.colors, sColorModes, 2, HOST_APPLY_VIDEO },
    { "lcd ghosting", "each frame fades out slowly, as on the gba's screen.", OPT_CHOICE,
      &gHostSettings.ghosting, sOffOn, 2, HOST_APPLY_VIDEO },
};

static const char *const sHuds[HOST_HUD_COUNT] = { "on", "off", "borders" };

static const char *const sTimers[HOST_TIMER_COUNT] = { "off", "level", "level and run" };

static const struct Option sInterface[] = {
    { "hud", HOST_HUD_HELP, OPT_CHOICE, &gHostSettings.hud, sHuds, HOST_HUD_COUNT, 0,
      HudAvailable },
    { "show fps", "the frames shown each second, in the window's top left corner.", OPT_CHOICE,
      &gHostSettings.showFps, sOffOn, 2, 0 },
    { "input display", "the buttons held, at the foot of that side's border or the picture.",
      OPT_CHOICE, &gHostSettings.inputDisplay, sCorners, 3, 0 },
    { "speedrun timer", "top left: the level's time, its split against your best, the run's.",
      OPT_CHOICE, &gHostSettings.speedrunTimer, sTimers, HOST_TIMER_COUNT, 0 },
    { "pause stats", "the pause menu's monitor also shows play time, deaths and crates.",
      OPT_CHOICE, &gHostSettings.pauseStats, sOffOn, 2, 0 },
};

static const char *const sQualities[] = { "original", "high" };

static const struct Option sAudio[] = {
    { "quality", "high: smoother, clearer sound. original: the gba's own.", OPT_CHOICE,
      &gHostSettings.audioQuality, sQualities, 2, HOST_APPLY_AUDIO },
    { "master volume", "everything the game plays.", OPT_PERCENT, &gHostSettings.master, NULL, 0,
      HOST_APPLY_AUDIO, NULL, NULL, NULL, 0, 100 },
    { "music volume", "the music.", OPT_PERCENT, &gHostSettings.music, NULL, 0, HOST_APPLY_AUDIO,
      NULL, NULL, NULL, 0, 100 },
    { "sound fx volume", "the sound effects.", OPT_PERCENT, &gHostSettings.sfx, NULL, 0,
      HOST_APPLY_AUDIO, NULL, NULL, NULL, 0, 100 },
};

static const char *const sRumbles[] = { "off", "light", "strong" };

static const char *const sPrompts[HOST_PROMPTS_COUNT] = { "auto",        "gba",      "xbox",
                                                          "playstation", "nintendo", "keyboard" };

static const struct Option sControls[] = {
    { "keyboard", "change the keys.", OPT_ACTION, NULL, NULL, 0, 0, NULL, NULL, OpenKeyboard },
    { "controller", "change the controller's buttons.", OPT_ACTION, NULL, NULL, 0, 0, NULL, NULL,
      OpenController },
    { "stick deadzone", "how far a stick or trigger must move before it counts.", OPT_PERCENT,
      &gHostSettings.deadzone, NULL, 0, 0, NULL, NULL, NULL, 10, 90 },
    { "rumble", "the controller buzzes with hits, explosions, crates and bosses.", OPT_CHOICE,
      &gHostSettings.rumble, sRumbles, 3, 0 },
    { "button prompts", "the game's a, b, l and r buttons drawn as your controller's or keys.",
      OPT_CHOICE, &gHostSettings.prompts, sPrompts, HOST_PROMPTS_COUNT, 0 },
};

static const char *const sAutoSaves[HOST_AUTOSAVE_COUNT] = { "off", "slot in use", "slot 4" };

static const char *const sSlideGraces[HOST_SLIDE_GRACE_COUNT] = { "off", "4 frames", "6 frames",
                                                                  "8 frames", "10 frames" };

static const struct Option sGameplay[] = {
    { "auto save", "after a level with something new: save to the slot in use, or slot 4.",
      OPT_CHOICE, &gHostSettings.autoSave, sAutoSaves, HOST_AUTOSAVE_COUNT, 0 },
    { "look up/down", "hold up or down while standing still to see above or below.", OPT_CHOICE,
      &gHostSettings.lookAround, sOffOn, 2, 0 },
    { "slide jump grace", "as in crash 2 and 3: a slide off a ledge can still jump for a moment.",
      OPT_CHOICE, &gHostSettings.slideGrace, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "ledge jump grace",
      "coyote time: walking or running off a ledge can still jump for a moment.", OPT_CHOICE,
      &gHostSettings.ledgeGrace, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "jump buffer", "a pressed just before landing still jumps as you land.", OPT_CHOICE,
      &gHostSettings.jumpBuffer, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "relaxed double jump", "the double jump works for the whole fall, not just near the top.",
      OPT_CHOICE, &gHostSettings.lateDoubleJump, sOffOn, 2, 0 },
    { "infinite lives", "no lives are lost, and no game over.", OPT_CHOICE,
      &gHostSettings.infiniteLives, sOffOn, 2, 0 },
};

static const char *const sDiscordStates[HOST_DISCORD_STATUS_COUNT] = {
    "off", "no app id", "waiting for discord", "connected", "app id refused",
};

static int DiscordOn(void)
{
    return gHostSettings.discord;
}

static const struct Option sExtras[] = {
    { "discord status", "your level and progress on your discord profile, while discord runs.",
      OPT_CHOICE, &gHostSettings.discord, sOffOn, 2, 0 },
    { "show progress", "completion, crystals, gems and relics in the status too.", OPT_CHOICE,
      &gHostSettings.discordProgress, sOffOn, 2, 0, NULL, DiscordOn },
    { "connection", "the discord app it uses: the discord app id line in settings.ini.", OPT_CHOICE,
      &gHostDiscordStatus, sDiscordStates, HOST_DISCORD_STATUS_COUNT, 0, NULL, Never },
    { "add to steam", "puts the game in your steam library with its art. grey while steam runs.",
      OPT_ACTION, NULL, NULL, 0, 0, NULL, HostSteamCanAdd, HostSteamAdd },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#ifdef HOST_ANDROID
/* The Android app's menu (phone.c keeps the settings; the app draws the
 * picture and the touch controls as they say). */
static const char *const sTouchSizes[] = { "small", "medium", "large", "huge" };
static const char *const sTouchMenus[] = { "levels", "always" };
static const char *const sHaptics[] = { "off", "light", "strong" };
static const char *const sTouchIcons[] = { "changing", "fixed" };
static const char *const sDpadStyles[] = { "cross", "stick", "buttons" };
void HostStreamEditLayout(void); /* stream.c */
static const char *const sTextSizes[] = { "normal", "large" };

static const struct Option sPhoneGeneral[] = {
    { "skip intro", "from the next launch: straight to the title, no logos or intro.", OPT_CHOICE,
      &gHostSettings.skipIntro, sOffOn, 2, 0 },
};

static const struct Option sPhoneDisplay[] = {
    { "frame rate", "120: extra frames between the game's own, on 120 hz screens.", OPT_CHOICE,
      &gHostSettings.frameRate, sFpsNames, HOST_FPS_COUNT, HOST_APPLY_VIDEO, FpsAvailable },
    { "scaling", "fit keeps the shape, integer keeps pixels even.", OPT_CHOICE,
      &gHostSettings.scaling, sScalings, HOST_SCALING_COUNT, HOST_APPLY_VIDEO },
    { "filter", "how the pixels are scaled up. cartoon smooths the outlines.", OPT_CHOICE,
      &gHostSettings.filter, sFilters, HOST_FILTER_COUNT, HOST_APPLY_VIDEO },
    { "scanlines", "dark lines between the rows of pixels, like a crt.", OPT_CHOICE,
      &gHostSettings.scanlines, sScanlines, 3, HOST_APPLY_VIDEO },
    { "lcd grid", "the gaps between the pixels of the gba's screen.", OPT_CHOICE,
      &gHostSettings.lcdGrid, sOffOn, 2, HOST_APPLY_VIDEO },
    { "colours", "the game's own colours, or as the gba's darker screen shows them.", OPT_CHOICE,
      &gHostSettings.colors, sColorModes, 2, HOST_APPLY_VIDEO },
    { "ghosting", "lcd ghosting: each frame fades out slowly, as on the gba's screen.", OPT_CHOICE,
      &gHostSettings.ghosting, sOffOn, 2, HOST_APPLY_VIDEO },
    { "borders", "beside the picture: black, its colours blurred, or the world's art.", OPT_CHOICE,
      &gHostSettings.borders, sBorders, HOST_BORDERS_COUNT, HOST_APPLY_VIDEO, NULL, NotStretched },
};

static const struct Option sPhoneTouch[] = {
    { "size", "how big the on-screen buttons are.", OPT_CHOICE, &gHostSettings.touchSize,
      sTouchSizes, 4, HOST_APPLY_VIDEO },
    { "opacity", "how solid the on-screen buttons are.", OPT_PERCENT, &gHostSettings.touchOpacity,
      NULL, 0, HOST_APPLY_VIDEO, NULL, NULL, NULL, 20, 100 },
    { "in menus", "levels: only in levels. menus and the warp room are tapped.", OPT_CHOICE,
      &gHostSettings.touchInMenus, sTouchMenus, 2, HOST_APPLY_VIDEO },
    { "haptics", "a buzz as you press an on-screen button.", OPT_CHOICE, &gHostSettings.haptics,
      sHaptics, 3, HOST_APPLY_VIDEO },
    { "d-pad", "cross: the gba's. stick: a joystick. buttons: each way its own button.", OPT_CHOICE,
      &gHostSettings.dpadStyle, sDpadStyles, 3, HOST_APPLY_VIDEO },
    { "floating", "on: the d-pad appears wherever your left thumb lands.", OPT_CHOICE,
      &gHostSettings.dpadFloating, sOffOn, 2, HOST_APPLY_VIDEO },
    { "vibration", "the phone buzzes with hits, explosions, crates and bosses.", OPT_CHOICE,
      &gHostSettings.rumble, sHaptics, 3, 0 },
    { "icons", "changing: each button shows what it does right now. fixed: always the same.",
      OPT_CHOICE, &gHostSettings.touchIcons, sTouchIcons, 2, 0 },
    { "layout", "move and resize the buttons: drag them, pinch, or - and +.", OPT_ACTION, NULL,
      NULL, 0, 0, NULL, NULL, HostStreamEditLayout },
};

static const struct Option sPhoneInterface[] = {
    { "text size", "this menu's text: normal, or large and easier to read.", OPT_CHOICE,
      &gHostSettings.textSize, sTextSizes, 2, 0 },
    { "hud", HOST_HUD_HELP, OPT_CHOICE, &gHostSettings.hud, sHuds, HOST_HUD_COUNT, 0,
      HudAvailable },
    { "prompts", "the game's a, b, l and r buttons drawn as your controller's.", OPT_CHOICE,
      &gHostSettings.prompts, sPrompts, HOST_PROMPTS_NINTENDO + 1, 0 },
    { "pause stats", "the pause menu's monitor also shows play time, deaths and crates.",
      OPT_CHOICE, &gHostSettings.pauseStats, sOffOn, 2, 0 },
};

static const struct Option sPhoneAudio[] = {
    { "quality", "high: smoother, clearer sound. original: the gba's own.", OPT_CHOICE,
      &gHostSettings.audioQuality, sQualities, 2, HOST_APPLY_AUDIO },
    { "master", "everything the game plays.", OPT_PERCENT, &gHostSettings.master, NULL, 0,
      HOST_APPLY_AUDIO, NULL, NULL, NULL, 0, 100 },
    { "music", "the music.", OPT_PERCENT, &gHostSettings.music, NULL, 0, HOST_APPLY_AUDIO, NULL,
      NULL, NULL, 0, 100 },
    { "sound fx", "the sound effects.", OPT_PERCENT, &gHostSettings.sfx, NULL, 0, HOST_APPLY_AUDIO,
      NULL, NULL, NULL, 0, 100 },
};

static const char *const sPhoneAutoSaves[HOST_AUTOSAVE_COUNT] = { "off", "in use", "slot 4" };

static const char *const sRelaxed[] = { "normal", "relaxed" };
static const char *const sLives[] = { "normal", "infinite" };

static const struct Option sPhoneGameplay[] = {
    { "auto save", "after a level with something new: save to the slot in use, or slot 4.",
      OPT_CHOICE, &gHostSettings.autoSave, sPhoneAutoSaves, HOST_AUTOSAVE_COUNT, 0 },
    { "look around", "hold up or down while standing still to see above or below.", OPT_CHOICE,
      &gHostSettings.lookAround, sOffOn, 2, 0 },
    { "slide grace", "as in crash 2 and 3: a slide off a ledge can still jump for a moment.",
      OPT_CHOICE, &gHostSettings.slideGrace, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "ledge", "ledge grace, coyote time: walking or running off a ledge can still jump a moment.",
      OPT_CHOICE, &gHostSettings.ledgeGrace, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "buffer", "jump buffer: a pressed just before landing still jumps as you land.", OPT_CHOICE,
      &gHostSettings.jumpBuffer, sSlideGraces, HOST_SLIDE_GRACE_COUNT, 0 },
    { "double jump", "relaxed: it works for the whole fall, not just near the top.", OPT_CHOICE,
      &gHostSettings.lateDoubleJump, sRelaxed, 2, 0 },
    { "lives", "infinite: no lives are lost, and no game over.", OPT_CHOICE,
      &gHostSettings.infiniteLives, sLives, 2, 0 },
};

static const struct Category sCategories[] = {
    { "general", "skipping the intro", sPhoneGeneral, COUNT(sPhoneGeneral) },
    { "display", "frame rate, scaling, filters, screen effects and borders", sPhoneDisplay,
      COUNT(sPhoneDisplay) },
    { "touch", "the on-screen buttons: size, d-pad, haptics, vibration, icons, layout", sPhoneTouch,
      COUNT(sPhoneTouch) },
    { "interface", "text size, hud, button prompts and pause stats", sPhoneInterface,
      COUNT(sPhoneInterface) },
    { "audio", "quality, and master, music and sound fx volume", sPhoneAudio, COUNT(sPhoneAudio) },
    { "gameplay", "auto save, looking around, jumps and lives", sPhoneGameplay,
      COUNT(sPhoneGameplay) },
};
#define SETTINGS_TITLE "settings"
#else
#define SETTINGS_TITLE "pc settings"
static const struct Category sCategories[] = {
    { "general", "portable mode, skip intro and quitting", sGeneral, COUNT(sGeneral) },
    { "display", "window, frame rate and scaling", sDisplay, COUNT(sDisplay) },
    { "graphics", "filters and screen effects", sGraphics, COUNT(sGraphics) },
    { "interface", "hud, fps counter, input display, timer and pause stats", sInterface,
      COUNT(sInterface) },
    { "audio", "quality, and master, music and sound fx volume", sAudio, COUNT(sAudio) },
    { "controls", "keyboard, controller, rumble and button prompts", sControls, COUNT(sControls) },
    { "gameplay", "auto save, looking up and down, jumps and lives", sGameplay, COUNT(sGameplay) },
    { "extras", "discord status and adding to steam", sExtras, COUNT(sExtras) },
};
#endif

static const char *const sBindNames[HOST_BIND_COUNT] = {
    "up",       "down",  "left",   "right",         "A button",   "B button",   "L button",
    "R button", "start", "select", "settings menu", "fullscreen", "screenshot", "fast forward",
};

static int OptionEnabled(const struct Option *o)
{
    return o->enabled == NULL || o->enabled();
}

static int ChoiceAvailable(const struct Option *o, int v)
{
    return o->available == NULL || o->available(v);
}

/* --- Input --------------------------------------------------------------- */

void HostUiOpen(void)
{
    sOpen = 1;
    sPage = PAGE_CATEGORIES;
    sConfirm = 0;
    sCapturing = 0;
    sMessageTime = 0;
}

/* On a bindings page, where Delete clears a binding (sdl.c's peek waits). */
int HostUiOnBindings(void)
{
    return HostUiIsOpen() && sPage == PAGE_BINDINGS;
}

int HostUiIsOpen(void)
{
    return sOpen;
}

int HostUiCapturing(void)
{
    return sCapturing ? 1 + sBindPad : 0;
}

void HostUiCaptured(int value)
{
    int (*table)[HOST_BIND_SLOTS] = sBindPad ? gHostSettings.pad : gHostSettings.keys;
    int i;
    int s;

    if (!sCapturing) {
        return;
    }
    /* A key or button bound twice to the same thing would be pointless:
     * this slot takes it, the other slot of the row loses it. */
    for (i = 0; i < HOST_BIND_COUNT; i++) {
        for (s = 0; s < HOST_BIND_SLOTS; s++) {
            if (i == sBindRow && table[i][s] == value) {
                table[i][s] = 0;
            }
        }
    }
    table[sBindRow][sBindSlot] = value;
    sCapturing = 0;
    HostConfigSave();
}

void HostUiCaptureCancel(void)
{
    sCapturing = 0;
}

static void RestoreBindings(void)
{
    if (sBindPad) {
        HostConfigDefaultPad(&gHostSettings);
    } else {
        HostConfigDefaultKeys(&gHostSettings);
    }
    HostConfigSave();
    Message("the defaults are back.");
}

static void Change(const struct Option *o, int dir)
{
    int v;

    if (!OptionEnabled(o) || o->value == NULL) {
        return;
    }
    v = *o->value;
    if (o->type == OPT_PERCENT) {
        v += dir * 5;
        if (v < o->min) {
            v = o->min;
        }
        if (v > o->max) {
            v = o->max;
        }
    } else if (o->type == OPT_CHOICE) {
        int tries;

        for (tries = 0; tries < o->count; tries++) {
            v += dir;
            if (v < 0 || v >= o->count) {
                return; /* the ends don't wrap, as the pause menu's volumes */
            }
            if (ChoiceAvailable(o, v)) {
                break;
            }
        }
        if (!ChoiceAvailable(o, v)) {
            return;
        }
    } else {
        return;
    }
    if (v == *o->value) {
        return;
    }
    *o->value = v;
    if (o->apply) {
        HostFrontendApply(o->apply);
    }
    HostConfigSave();
}

static void InputOptions(int action)
{
    const struct Category *cat = &sCategories[sCategory];
    int *cursor = &sCursor[sCategory];
    const struct Option *o = &cat->options[*cursor];

    switch (action) {
    case HOST_UI_UP:
        if (*cursor > 0) {
            (*cursor)--;
        }
        break;
    case HOST_UI_DOWN:
        if (*cursor < cat->count - 1) {
            (*cursor)++;
        }
        break;
    case HOST_UI_LEFT:
    case HOST_UI_RIGHT:
        if (o->type == OPT_ACTION && o->value != NULL) {
            if (OptionEnabled(o) && *o->value != (action == HOST_UI_RIGHT)) {
                o->action();
            }
        } else {
            Change(o, action == HOST_UI_RIGHT ? 1 : -1);
        }
        break;
    case HOST_UI_OK:
        if (!OptionEnabled(o)) {
            break;
        }
        if (o->type == OPT_ACTION) {
            o->action();
        } else if (o->type == OPT_CHOICE) {
            /* A on a choice steps it on, around at the end. */
            int before = *o->value;

            Change(o, 1);
            if (*o->value == before) {
                int v;

                for (v = 0; v < before; v++) {
                    if (ChoiceAvailable(o, v)) {
                        *o->value = v;
                        if (o->apply) {
                            HostFrontendApply(o->apply);
                        }
                        HostConfigSave();
                        break;
                    }
                }
            }
        }
        break;
    case HOST_UI_BACK:
        sPage = PAGE_CATEGORIES;
        break;
    default:
        break;
    }
}

static void InputBindings(int action)
{
    int rows = HOST_BIND_COUNT + 1; /* and "restore defaults" */

    switch (action) {
    case HOST_UI_UP:
        if (sBindRow > 0) {
            sBindRow--;
        }
        break;
    case HOST_UI_DOWN:
        if (sBindRow < rows - 1) {
            sBindRow++;
        }
        break;
    case HOST_UI_LEFT:
        sBindSlot = 0;
        break;
    case HOST_UI_RIGHT:
        sBindSlot = 1;
        break;
    case HOST_UI_OK:
        if (sBindRow == HOST_BIND_COUNT) {
            sConfirm = 1;
            sConfirmText = sBindPad ? "restore the controller's defaults?"
                                    : "restore the keyboard's defaults?";
            sConfirmYes = RestoreBindings;
        } else {
            sCapturing = 1;
        }
        break;
    case HOST_UI_CLEAR:
        if (sBindRow < HOST_BIND_COUNT) {
            (sBindPad ? gHostSettings.pad : gHostSettings.keys)[sBindRow][sBindSlot] = 0;
            HostConfigSave();
        }
        break;
    case HOST_UI_BACK:
        sPage = PAGE_OPTIONS;
        break;
    default:
        break;
    }
}

void HostUiInput(int action)
{
    if (!sOpen || sCapturing) {
        return;
    }
    if (sConfirm) {
        if (action == HOST_UI_OK) {
            sConfirm = 0;
            sConfirmYes();
        } else if (action == HOST_UI_BACK) {
            sConfirm = 0;
        }
        return;
    }
    switch (sPage) {
    case PAGE_CATEGORIES:
        if (action == HOST_UI_UP && sCategory > 0) {
            sCategory--;
        } else if (action == HOST_UI_DOWN && sCategory < COUNT(sCategories) - 1) {
            sCategory++;
        } else if (action == HOST_UI_OK && sCategories[sCategory].count != 0) {
            sPage = PAGE_OPTIONS;
            sTop = 0;
        } else if (action == HOST_UI_BACK) {
            sOpen = 0;
            HostConfigSave();
        }
        break;
    case PAGE_OPTIONS:
        InputOptions(action);
        break;
    case PAGE_BINDINGS:
        InputBindings(action);
        break;
    default:
        break;
    }
}

static void ValueText(const struct Option *o, char *out, int size);
static int ValueWidth(const char *value);

/* --- The mouse --------------------------------------------------------------- */

/* The row of the options and bindings pages under y, or -1. */
static int RowAt(int y, int count)
{
    int k;

    if (y < ROWS_TOP || y >= ROWS_TOP + ROW_PITCH * ROW_COUNT) {
        return -1;
    }
    k = sTop + (y - ROWS_TOP) / ROW_PITCH;
    return k < count ? k : -1;
}

/* The mouse at (x, y) on the menu's canvas (sdl.c maps the window to it):
 * moving it highlights what is under it, the left button does what A
 * does there (on a value, its left half steps it back and its right half
 * on), the right button what B does, and the wheel steps a value under it
 * or scrolls the rows. */
void HostUiMouse(int event, int x, int y)
{
    if (!sOpen || sCapturing) {
        return;
    }
    x -= HOST_ART_X0; /* the page is in the middle of the canvas (HostUiDraw) */
    Layout();
    if (sConfirm) {
        /* "A yes     B no": its halves, and the right button for no */
        if (event == HOST_UI_MOUSE_LEFT && y >= 82 && y < 100) {
            HostUiInput(x < W / 2 ? HOST_UI_OK : HOST_UI_BACK);
        } else if (event == HOST_UI_MOUSE_RIGHT) {
            HostUiInput(HOST_UI_BACK);
        }
        return;
    }
    if (event == HOST_UI_MOUSE_RIGHT) {
        HostUiInput(HOST_UI_BACK);
        return;
    }
    switch (sPage) {
    case PAGE_CATEGORIES:
        {
            int i = (y - 0x44) / 10;
            int over = x < 0x64 && y >= 0x44 && i >= 0 && i < COUNT(sCategories);

            if (Large()) {
                i = RowAt(y, COUNT(sCategories));
                over = i >= 0;
                if (event == HOST_UI_MOUSE_LEFT && i < 0) {
                    if (y < ROWS_TOP && y >= ROWS_TOP - 8) {
                        HostUiInput(HOST_UI_UP);
                    } else if (y >= ROWS_TOP + ROW_PITCH * ROW_COUNT &&
                               y < ROWS_TOP + ROW_PITCH * ROW_COUNT + 10) {
                        HostUiInput(HOST_UI_DOWN);
                    }
                    break;
                }
            }

            if (event == HOST_UI_MOUSE_WHEEL_UP || event == HOST_UI_MOUSE_WHEEL_DOWN) {
                HostUiInput(event == HOST_UI_MOUSE_WHEEL_UP ? HOST_UI_UP : HOST_UI_DOWN);
            } else if (over) {
                sCategory = i;
                if (event == HOST_UI_MOUSE_LEFT) {
                    HostUiInput(HOST_UI_OK);
                }
            }
            break;
        }
    case PAGE_OPTIONS:
        {
            const struct Category *cat = &sCategories[sCategory];
            int i = RowAt(y, cat->count);
            int onValue = 0;
            int left = 0;

            if (i >= 0) {
                char value[48];
                int vw;

                ValueText(&cat->options[i], value, sizeof(value));
                vw = ValueWidth(value);
                onValue =
                    value[0] != '\0' && x >= W - ROW_MARGIN - vw - 4 && x < W - ROW_MARGIN + 6;
                left = x < W - ROW_MARGIN - vw / 2;
            }
            if (event == HOST_UI_MOUSE_WHEEL_UP || event == HOST_UI_MOUSE_WHEEL_DOWN) {
                if (onValue) {
                    sCursor[sCategory] = i;
                    HostUiInput(event == HOST_UI_MOUSE_WHEEL_UP ? HOST_UI_RIGHT : HOST_UI_LEFT);
                } else {
                    HostUiInput(event == HOST_UI_MOUSE_WHEEL_UP ? HOST_UI_UP : HOST_UI_DOWN);
                }
                break;
            }
            if (event == HOST_UI_MOUSE_LEFT && i < 0) {
                /* the arrows above and below the rows scroll them */
                if (y < ROWS_TOP && y >= ROWS_TOP - 8) {
                    HostUiInput(HOST_UI_UP);
                } else if (y >= ROWS_TOP + ROW_PITCH * ROW_COUNT &&
                           y < ROWS_TOP + ROW_PITCH * ROW_COUNT + 10) {
                    HostUiInput(HOST_UI_DOWN);
                }
                break;
            }
            if (i < 0) {
                break;
            }
            sCursor[sCategory] = i;
            if (event == HOST_UI_MOUSE_LEFT) {
                const struct Option *o = &cat->options[i];

                if (onValue && o->type != OPT_ACTION) {
                    HostUiInput(left ? HOST_UI_LEFT : HOST_UI_RIGHT);
                } else {
                    HostUiInput(HOST_UI_OK);
                }
            }
            break;
        }
    case PAGE_BINDINGS:
        {
            int i = RowAt(y, HOST_BIND_COUNT + 1);

            if (event == HOST_UI_MOUSE_WHEEL_UP || event == HOST_UI_MOUSE_WHEEL_DOWN) {
                HostUiInput(event == HOST_UI_MOUSE_WHEEL_UP ? HOST_UI_UP : HOST_UI_DOWN);
                break;
            }
            if (i < 0) {
                if (event == HOST_UI_MOUSE_LEFT && y < ROWS_TOP && y >= ROWS_TOP - 8) {
                    HostUiInput(HOST_UI_UP);
                } else if (event == HOST_UI_MOUSE_LEFT && y >= ROWS_TOP + ROW_PITCH * ROW_COUNT &&
                           y < ROWS_TOP + ROW_PITCH * ROW_COUNT + 10) {
                    HostUiInput(HOST_UI_DOWN);
                }
                break;
            }
            sBindRow = i;
            if (i < HOST_BIND_COUNT) {
                if (x >= 108 && x < 170) {
                    sBindSlot = 0;
                } else if (x >= 170) {
                    sBindSlot = 1;
                } else if (event == HOST_UI_MOUSE_LEFT) {
                    break; /* the action's name: nothing to change there */
                }
            }
            if (event == HOST_UI_MOUSE_LEFT) {
                HostUiInput(HOST_UI_OK);
            }
            break;
        }
    default:
        break;
    }
}

/* --- Drawing the pages --------------------------------------------------- */

static void DrawHelp(const char *s, int pal)
{
    char lines[2][64];
    int n = Wrap(s, W - 16, lines, 2);
    int i;

    for (i = 0; i < n; i++) {
        TextCentered(0, W / 2, (n == 1 ? 40 : 35) + 10 * i, lines[i], pal);
    }
}

static void DrawRowsFrame(int count, int cursor);

/* Large text: the categories as the options pages' rows, the
 * highlighted one's description in the band under the title. */
static void DrawCategoryRows(void)
{
    int i;

    DrawWidePanel();
    TextCentered(1, W / 2, 14, SETTINGS_TITLE, HOST_UI_PAL_NORMAL);
    DrawRowsFrame(COUNT(sCategories), sCategory);
    for (i = sTop; i < COUNT(sCategories) && i < sTop + ROW_COUNT; i++) {
        int pal = i == sCategory ? HOST_UI_PAL_WHITE : HOST_UI_PAL_NORMAL;

        TextCentered(1, W / 2, ROWS_TOP + ROW_PITCH * (i - sTop) + 2, sCategories[i].name, pal);
    }
    DrawHelp(sCategories[sCategory].about, HOST_UI_PAL_NORMAL);
}

static void DrawCategories(void)
{
    const struct Category *cat = &sCategories[sCategory];
    char lines[6][64];
    int n;
    int i;

    if (Large()) {
        DrawCategoryRows();
        return;
    }
    DrawPauseScreen();
    TextCentered(1, W / 2, 14, SETTINGS_TITLE, HOST_UI_PAL_NORMAL);
    Text(1, 0x20, 0x26, HOST_PORT_VERSION, HOST_UI_PAL_NORMAL);
    /* The monitor's title, as the pause menu's page titles. */
    TextCentered(0, 194, 0x2D, cat->name, HOST_UI_PAL_NORMAL);
    for (i = 0; i < COUNT(sCategories); i++) {
        int pal;

        if (sCategories[i].count == 0) {
            pal = i == sCategory ? HOST_UI_PAL_GREY_LIGHT : HOST_UI_PAL_GREY;
        } else {
            pal = i == sCategory ? HOST_UI_PAL_WHITE : HOST_UI_PAL_NORMAL;
        }
        TextCentered(0, 0x32, 0x45 + 10 * i, sCategories[i].name, pal);
    }
    n = Wrap(cat->about, 66, lines, 6);
    for (i = 0; i < n; i++) {
        TextCentered(0, 194, 108 - n * 5 + 10 * i, lines[i],
                     cat->count ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY_LIGHT);
    }
}

static void ValueText(const struct Option *o, char *out, int size)
{
    if (o->type == OPT_PERCENT) {
        snprintf(out, size, "<%d%%>", *o->value);
    } else if (o->type == OPT_CHOICE) {
        snprintf(out, size, "<%s>", o->choices[*o->value]);
    } else if (o->value != NULL) {
        snprintf(out, size, "<%s>", o->choices[*o->value]);
    } else {
        snprintf(out, size, "%s", "");
    }
}

/* A value, "<75%>", as the rows show it: with large text, the title's
 * font between drawn arrows. */
static int ValueWidth(const char *value)
{
    char inner[48];
    size_t n = strlen(value);

    if (!Large() || n < 2 || value[0] != '<') {
        return HostUiTextWidth(ROW_FONT, value);
    }
    snprintf(inner, sizeof(inner), "%.*s", (int)(n - 2), value + 1);
    return HostUiTextWidth(1, inner) + 2 * ARROW_W;
}

/* A left or right pointing triangle, 4 wide and 7 high, at (x, y). */
static void SideArrow(int x, int y, int right, int pal)
{
    const u32 *c = sFontPal[pal];
    int r;

    for (r = 0; r < 7; r++) {
        int len = r < 4 ? r + 1 : 7 - r;
        int k;

        for (k = 0; k < len; k++) {
            int px = right ? x + k : x + 3 - k;

            sCanvas[(y + r) * W + px] = 0xFF000000 | c[k == len - 1 ? 6 : r < 3 ? 4 : 8];
        }
    }
}

static void DrawValue(int right, int y, const char *value, int pal)
{
    char inner[48];
    size_t n = strlen(value);
    int w;

    if (!Large() || n < 2 || value[0] != '<') {
        TextRight(ROW_FONT, right, y, value, pal);
        return;
    }
    snprintf(inner, sizeof(inner), "%.*s", (int)(n - 2), value + 1);
    w = HostUiTextWidth(1, inner);
    SideArrow(right - 4, y + 4, 1, pal);
    TextRight(1, right - ARROW_W, y, inner, pal);
    SideArrow(right - ARROW_W - w - ARROW_W + 1, y + 4, 0, pal);
}

static void DrawRowsFrame(int count, int cursor)
{
    if (cursor < sTop) {
        sTop = cursor;
    }
    if (cursor >= sTop + ROW_COUNT) {
        sTop = cursor - ROW_COUNT + 1;
    }
    if (sTop > 0) {
        Arrow(W / 2, ROWS_TOP - 3, 0);
    }
    if (sTop + ROW_COUNT < count) {
        Arrow(W / 2, ROWS_TOP + ROW_PITCH * ROW_COUNT + 1, 1);
    }
}

static void DrawOptions(void)
{
    const struct Category *cat = &sCategories[sCategory];
    int cursor = sCursor[sCategory];
    int i;

    DrawWidePanel();
    TextCentered(1, W / 2, 14, cat->name, HOST_UI_PAL_NORMAL);
    DrawRowsFrame(cat->count, cursor);
    for (i = sTop; i < cat->count && i < sTop + ROW_COUNT; i++) {
        const struct Option *o = &cat->options[i];
        int y = ROWS_TOP + ROW_PITCH * (i - sTop) + (Large() ? 2 : 2);
        int on = OptionEnabled(o);
        int pal = on ? (i == cursor ? HOST_UI_PAL_WHITE : HOST_UI_PAL_NORMAL)
                     : (i == cursor ? HOST_UI_PAL_GREY_LIGHT : HOST_UI_PAL_GREY);
        char value[48];

        Text(ROW_FONT, ROW_MARGIN, y, o->label, pal);
        ValueText(o, value, sizeof(value));
        if (value[0] != '\0') {
            int vpal = pal;

            if (on && o->type == OPT_CHOICE && !ChoiceAvailable(o, *o->value)) {
                vpal = i == cursor ? HOST_UI_PAL_GREY_LIGHT : HOST_UI_PAL_GREY;
            }
            DrawValue(W - ROW_MARGIN, y, value, vpal);
        }
    }
    if (sMessageTime > 0) {
        DrawHelp(sMessage, HOST_UI_PAL_WHITE);
    } else {
        const struct Option *o = &cat->options[cursor];

        if (o->value == &gHostSettings.frameRate && HostFrontendRefreshRate() > 0) {
            char text[160];

            snprintf(text, sizeof(text), "your display: %d hz. same game speed at any rate.",
                     HostFrontendRefreshRate());
            DrawHelp(text, HOST_UI_PAL_NORMAL);
        } else {
            DrawHelp(o->help, OptionEnabled(o) ? HOST_UI_PAL_NORMAL : HOST_UI_PAL_GREY_LIGHT);
        }
    }
}

static void Fit(char *s, int width)
{
    int len = (int)strlen(s);

    while (len > 0 && HostUiTextWidth(0, s) > width) {
        s[--len] = '\0';
    }
}

static void DrawBindings(void)
{
    int rows = HOST_BIND_COUNT + 1;
    int (*table)[HOST_BIND_SLOTS] = sBindPad ? gHostSettings.pad : gHostSettings.keys;
    int i;

    DrawWidePanel();
    TextCentered(1, W / 2, 14, sBindPad ? "controller" : "keyboard", HOST_UI_PAL_NORMAL);
    DrawRowsFrame(rows, sBindRow);
    for (i = sTop; i < rows && i < sTop + ROW_COUNT; i++) {
        int y = ROWS_TOP + ROW_PITCH * (i - sTop) + 2;
        int s;

        if (i == HOST_BIND_COUNT) {
            TextCentered(0, W / 2, y, "restore defaults",
                         i == sBindRow ? HOST_UI_PAL_WHITE : HOST_UI_PAL_NORMAL);
            continue;
        }
        Text(0, 18, y, sBindNames[i], i == sBindRow ? HOST_UI_PAL_WHITE : HOST_UI_PAL_NORMAL);
        for (s = 0; s < HOST_BIND_SLOTS; s++) {
            char name[48];
            int x = s == 0 ? 112 : 172;
            int pal;

            if (i == sBindRow && s == sBindSlot && sCapturing) {
                snprintf(name, sizeof(name), "%s", "...");
            } else if (table[i][s] == 0) {
                snprintf(name, sizeof(name), "%s", "-");
            } else {
                snprintf(name, sizeof(name), "%s",
                         sBindPad ? gHostUiPadName(table[i][s]) : gHostUiKeyName(table[i][s]));
            }
            Fit(name, 56);
            pal = (i == sBindRow && s == sBindSlot)
                      ? HOST_UI_PAL_WHITE
                      : (table[i][s] == 0 ? HOST_UI_PAL_GREY : HOST_UI_PAL_NORMAL);
            Text(0, x, y, name, pal);
        }
    }
    if (sCapturing) {
        char text[96];

        snprintf(text, sizeof(text),
                 sBindPad ? "press a button for %s. escape cancels."
                          : "press a key for %s. escape cancels.",
                 sBindNames[sBindRow]);
        DrawHelp(text, HOST_UI_PAL_WHITE);
    } else if (sMessageTime > 0) {
        DrawHelp(sMessage, HOST_UI_PAL_WHITE);
    } else {
        DrawHelp(sBindPad
                     ? "A changes, delete or the controller's x clears. two of each, either works."
                     : "A changes, delete clears. two keys for each, either works.",
                 HOST_UI_PAL_NORMAL);
    }
}

static void DrawConfirm(void)
{
    int x0 = 36;
    int x1 = W - 36;
    int y0 = 58;
    int y1 = 106;
    int x;
    int y;

    Dim(0, 0, W, H);
    /* A box from the pause screen's monitor frame colours. */
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            int edge = x < x0 + 2 || x >= x1 - 2 || y < y0 + 2 || y >= y1 - 2;
            int ay = edge ? 151 : 100;
            int ax = edge ? 50 : 50;

            sCanvas[y * W + x] = Art(ax, ay);
        }
    }
    TextCentered(0, W / 2, y0 + 10, sConfirmText, HOST_UI_PAL_WHITE);
    TextCentered(0, W / 2, y0 + 30, "A yes     B no", HOST_UI_PAL_NORMAL);
}

static void DrawPage(u32 *canvas);

/* The menu on `canvas` (HOST_UI_WIDTH x HOST_UI_HEIGHT). Widescreen: the
 * 240-pixel page in the middle, and beside it the pause screen's frame
 * gone on, as the game's pause menu has (HostWideArtSource). */
void HostUiDraw(u32 *canvas)
{
    static u32 page[W * H];
    int x;
    int y;

    if (!HOST_WIDE) {
        DrawPage(canvas);
        return;
    }
    DrawPage(page);
    for (y = 0; y < H; y++) {
        for (x = 0; x < HOST_UI_WIDTH; x++) {
            int ax;
            int ay;

            if (HostWideArtSource(x, y, &ax, &ay)) {
                /* a question on top dims the page (DrawConfirm's Dim): the bars too */
                u32 bar = sConfirm ? (Art(ax, ay) >> 2) & 0x3F3F3F : Art(ax, ay) & 0xFFFFFF;

                canvas[y * HOST_UI_WIDTH + x] = 0xFF000000 | bar;
            } else {
                canvas[y * HOST_UI_WIDTH + x] = page[y * W + ax];
            }
        }
    }
}

static void DrawPage(u32 *canvas)
{
    InitPalettes();
    Layout();
    sCanvas = canvas;
    if (sMessageTime > 0) {
        sMessageTime--;
    }
    switch (sPage) {
    case PAGE_CATEGORIES:
        DrawCategories();
        break;
    case PAGE_OPTIONS:
        DrawOptions();
        break;
    case PAGE_BINDINGS:
        DrawBindings();
        break;
    default:
        break;
    }
    if (sConfirm) {
        DrawConfirm();
    }
}

/* --- --ui-preview ---------------------------------------------------------- */

void HostUiPreview(const char *dir)
{
    static u32 canvas[HOST_UI_WIDTH_MAX * H];
    char path[512];
    int c;
    int n = 0;

    HostConfigDefaults(&gHostSettings);
    /* HOST_UI_LARGE: the pages with large text */
    gHostSettings.textSize = getenv("HOST_UI_LARGE") != NULL;
    HostUiOpen();
    /* Every help must fit the band's two lines. */
    for (c = 0; c < COUNT(sCategories); c++) {
        int i;

        for (i = 0; i < sCategories[c].count; i++) {
            char lines[4][64];

            const struct Option *o = &sCategories[c].options[i];
            int v;

            if (Wrap(o->help, W - 16, lines, 4) > 2) {
                printf("ui-preview: too long: %s\n", o->help);
            }
            /* a row's label and each of its values side by side */
            for (v = 0; v < (o->type == OPT_CHOICE ? o->count : 1); v++) {
                char value[48];
                int saved = o->value != NULL ? *o->value : 0;
                int width;

                if (o->value == NULL) {
                    break;
                }
                *o->value = o->type == OPT_PERCENT ? 100 : v;
                ValueText(o, value, sizeof(value));
                *o->value = saved;
                width = HostUiTextWidth(ROW_FONT, o->label) + 4 + ValueWidth(value);
                if (width > W - 2 * ROW_MARGIN) {
                    printf("ui-preview: too wide: %s %s (%d)\n", o->label, value, width);
                }
            }
        }
    }
    for (c = 0; c < COUNT(sCategories); c++) {
        sPage = PAGE_CATEGORIES;
        sCategory = c;
        HostUiDraw(canvas);
        snprintf(path, sizeof(path), "%s/%02d-categories-%s.png", dir, n++, sCategories[c].name);
        HostWritePng(path, canvas, HOST_UI_WIDTH, H);
        if (sCategories[c].count != 0) {
            sPage = PAGE_OPTIONS;
            sCursor[c] = sCategories[c].count > 2 ? 2 : 0;
            sTop = 0;
            HostUiDraw(canvas);
            snprintf(path, sizeof(path), "%s/%02d-options-%s.png", dir, n++, sCategories[c].name);
            HostWritePng(path, canvas, HOST_UI_WIDTH, H);
        }
    }
    for (c = 0; c < 2; c++) {
        sPage = PAGE_BINDINGS;
        sBindPad = c;
        sBindRow = 4;
        sBindSlot = 0;
        sTop = 0;
        HostUiDraw(canvas);
        snprintf(path, sizeof(path), "%s/%02d-bindings-%d.png", dir, n++, c);
        HostWritePng(path, canvas, HOST_UI_WIDTH, H);
    }
    sPage = PAGE_OPTIONS;
    sCategory = 0;
    AskQuit();
    HostUiDraw(canvas);
    snprintf(path, sizeof(path), "%s/%02d-confirm.png", dir, n++);
    HostWritePng(path, canvas, HOST_UI_WIDTH, H);
    sConfirm = 0;
}
