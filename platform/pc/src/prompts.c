/* Button prompts (docs/platforms.md, "Button prompts"; Controls' Button
 * prompts): the game's text draws the GBA's A, B, L and R as small
 * glyphs (the small font's last four); these draw them as the buttons or
 * keys they are bound to instead, in the style of the pad in use: an
 * Xbox pad's coloured letters, a PlayStation pad's symbols, a Nintendo
 * pad's letters, or keyboard keys.
 *
 * The art is the GBA's size, made here from a few shapes and a 3x5
 * font, so it goes through the filters and the frame interpolation as
 * the game's own pixels do. prompt_hook.cpp says where the font's button
 * glyphs are in OBJ VRAM; the PPUs (ppu.c, ppu_hd.c) draw an OBJ of those
 * tiles from gHostPrompts' art and colours instead (HostPromptGlyph). A
 * slot that is off keeps the game's glyph: the GBA style, or a binding
 * with no fitting picture (a face button's glyph is 7 pixels wide, so a
 * shoulder button or a long key name can't go there).
 *
 * sdl.c picks the style and calls HostPromptForPad/HostPromptForKey each
 * picture; HOST_PROMPTS=xbox|playstation|nintendo (headless runs, with
 * the default bindings) does the same for the tests. */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "host_platform.h"
#include "host_ui.h"

struct HostPrompts gHostPrompts = { 0, 0, 0x80000000u };

/* --- Colours -------------------------------------------------------------- */

static const struct {
    char name;
    u8 r, g, b;
} sColours[] = {
    { 'k', 32, 24, 16 },    /* the font's shadow */
    { 'w', 248, 248, 248 }, /* letters */
    { 'l', 208, 208, 216 }, /* a key's top */
    { 'm', 140, 140, 150 }, /* a key's side */
    { 'd', 64, 64, 72 },    /* a dark button */
    { 'e', 104, 104, 114 }, /* its rim */
    { 'n', 40, 40, 48 },    /* dark letters */
    { 'G', 72, 176, 48 },   /* Xbox A */
    { 'R', 224, 48, 48 },   /* Xbox B */
    { 'B', 40, 112, 224 },  /* Xbox X */
    { 'Y', 240, 192, 32 },  /* Xbox Y */
    { 'c', 124, 178, 240 }, /* PlayStation cross */
    { 'o', 255, 96, 96 },   /* circle */
    { 's', 240, 128, 216 }, /* square */
    { 't', 64, 226, 160 },  /* triangle */
};

static u16 Bgr555(int c)
{
    return (u16)((sColours[c].r >> 3) | (sColours[c].g >> 3) << 5 | (sColours[c].b >> 3) << 10);
}

/* --- The 3x5 font ----------------------------------------------------------- */

static const struct {
    char c;
    const char *rows; /* 5 rows of 3 */
} sFont[] = {
    { 'A', ".#."
           "#.#"
           "###"
           "#.#"
           "#.#" },
    { 'B', "##."
           "#.#"
           "##."
           "#.#"
           "##." },
    { 'C', ".##"
           "#.."
           "#.."
           "#.."
           ".##" },
    { 'D', "##."
           "#.#"
           "#.#"
           "#.#"
           "##." },
    { 'E', "###"
           "#.."
           "##."
           "#.."
           "###" },
    { 'F', "###"
           "#.."
           "##."
           "#.."
           "#.." },
    { 'G', ".##"
           "#.."
           "#.#"
           "#.#"
           ".##" },
    { 'H', "#.#"
           "#.#"
           "###"
           "#.#"
           "#.#" },
    { 'I', "###"
           ".#."
           ".#."
           ".#."
           "###" },
    { 'J', "..#"
           "..#"
           "..#"
           "#.#"
           ".#." },
    { 'K', "#.#"
           "#.#"
           "##."
           "#.#"
           "#.#" },
    { 'L', "#.."
           "#.."
           "#.."
           "#.."
           "###" },
    { 'M', "#.#"
           "###"
           "###"
           "#.#"
           "#.#" },
    { 'N', "##."
           "#.#"
           "#.#"
           "#.#"
           "#.#" },
    { 'O', ".#."
           "#.#"
           "#.#"
           "#.#"
           ".#." },
    { 'P', "##."
           "#.#"
           "##."
           "#.."
           "#.." },
    { 'Q', ".#."
           "#.#"
           "#.#"
           "##."
           ".##" },
    { 'R', "##."
           "#.#"
           "##."
           "#.#"
           "#.#" },
    { 'S', ".##"
           "#.."
           ".#."
           "..#"
           "##." },
    { 'T', "###"
           ".#."
           ".#."
           ".#."
           ".#." },
    { 'U', "#.#"
           "#.#"
           "#.#"
           "#.#"
           "###" },
    { 'V', "#.#"
           "#.#"
           "#.#"
           "#.#"
           ".#." },
    { 'W', "#.#"
           "#.#"
           "###"
           "###"
           "#.#" },
    { 'X', "#.#"
           "#.#"
           ".#."
           "#.#"
           "#.#" },
    { 'Y', "#.#"
           "#.#"
           ".#."
           ".#."
           ".#." },
    { 'Z', "###"
           "..#"
           ".#."
           "#.."
           "###" },
    { '0', ".#."
           "#.#"
           "#.#"
           "#.#"
           ".#." },
    { '1', ".#."
           "##."
           ".#."
           ".#."
           "###" },
    { '2', "##."
           "..#"
           ".#."
           "#.."
           "###" },
    { '3', "##."
           "..#"
           ".#."
           "..#"
           "##." },
    { '4', "#.#"
           "#.#"
           "###"
           "..#"
           "..#" },
    { '5', "###"
           "#.."
           "##."
           "..#"
           "##." },
    { '6', ".##"
           "#.."
           "##."
           "#.#"
           ".#." },
    { '7', "###"
           "..#"
           ".#."
           ".#."
           ".#." },
    { '8', ".#."
           "#.#"
           ".#."
           "#.#"
           ".#." },
    { '9', ".#."
           "#.#"
           ".##"
           "..#"
           "##." },
    { '-', "..."
           "..."
           "###"
           "..."
           "..." },
    { '=', "..."
           "###"
           "..."
           "###"
           "..." },
    { '.', "..."
           "..."
           "..."
           "..."
           ".#." },
    { ',', "..."
           "..."
           "..."
           ".#."
           "#.." },
    { '/', "..#"
           "..#"
           ".#."
           "#.."
           "#.." },
    { ';', "..."
           ".#."
           "..."
           ".#."
           "#.." },
    { '\'', ".#."
            ".#."
            "..."
            "..."
            "..." },
    { '[', "##."
           "#.."
           "#.."
           "#.."
           "##." },
    { ']', ".##"
           "..#"
           "..#"
           "..#"
           ".##" },
    { '\\', "#.."
            "#.."
            ".#."
            "..#"
            "..#" },
    { '`', "#.."
           ".#."
           "..."
           "..."
           "..." },
};

static const char *Glyph35(char c)
{
    size_t i;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (i = 0; i < sizeof(sFont) / sizeof(sFont[0]); i++) {
        if (sFont[i].c == c) {
            return sFont[i].rows;
        }
    }
    return NULL;
}

/* --- Drawing -------------------------------------------------------------- */

/* A picture being made: colour names on a 16x16 grid. */
static char sCanvas[16][16];

static void Put(int x, int y, char c)
{
    if (x >= 0 && y >= 0 && x < 16 && y < 16) {
        sCanvas[y][x] = c;
    }
}

static void Text(int x, int y, const char *s, char ink)
{
    for (; *s; s++, x += 4) {
        const char *rows = Glyph35(*s);
        int r;
        int col;

        for (r = 0; rows != NULL && r < 5; r++) {
            for (col = 0; col < 3; col++) {
                if (rows[r * 3 + col] == '#') {
                    Put(x + col, y + r, ink);
                }
            }
        }
    }
}

static int TextWidth(const char *s)
{
    int n = (int)strlen(s);

    return n > 0 ? n * 4 - 1 : 0;
}

/* A round button, 7x7, its top left at x, y. */
static void Circle(int x, int y, char body, char rim)
{
    static const char *const shape[7] = { "..###..", ".#####.", "#######", "#######",
                                          "#######", ".#####.", "..###.." };
    int r;
    int c;

    for (r = 0; r < 7; r++) {
        for (c = 0; c < 7; c++) {
            if (shape[r][c] == '#') {
                int edge = r == 0 || r == 6 || c == 0 || c == 6 || (r == 1 && (c == 1 || c == 5)) ||
                           (r == 5 && (c == 1 || c == 5));

                Put(x + c, y + r, rim != 0 && edge ? rim : body);
            }
        }
    }
}

/* A key or a pill, w x 7, corners cut; a key's bottom row is its side. */
static void Box(int x, int y, int w, char body, char rim, char side)
{
    int r;
    int c;

    for (r = 0; r < 7; r++) {
        for (c = 0; c < w; c++) {
            int corner = (r == 0 || r == 6) && (c == 0 || c == w - 1);
            int edge = r == 0 || r == 6 || c == 0 || c == w - 1;

            if (corner) {
                continue;
            }
            Put(x + c, y + r, r == 6 && side ? side : (rim != 0 && edge ? rim : body));
        }
    }
}

/* The PlayStation symbols, 5x5, drawn on a circle at x, y. */
static void Symbol(int x, int y, int which)
{
    static const char *const sym[4][5] = {
        { "#...#", ".#.#.", "..#..", ".#.#.", "#...#" }, /* cross */
        { ".###.", "#...#", "#...#", "#...#", ".###." }, /* circle */
        { "#####", "#...#", "#...#", "#...#", "#####" }, /* square */
        { ".....", "..#..", ".#.#.", "#...#", "#####" }, /* triangle */
    };
    static const char ink[4] = { 'c', 'o', 's', 't' };
    int r;
    int c;

    for (r = 0; r < 5; r++) {
        for (c = 0; c < 5; c++) {
            if (sym[which][r][c] == '#') {
                Put(x + 1 + c, y + 1 + r, ink[which]);
            }
        }
    }
}

/* The font's shadow: down and right of every pixel. */
static void Shadow(void)
{
    char out[16][16];
    int x;
    int y;

    memcpy(out, sCanvas, sizeof(out));
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            if (sCanvas[y][x] != 0 && x + 1 < 16 && y + 1 < 16 && sCanvas[y + 1][x + 1] == 0) {
                out[y + 1][x + 1] = 'k';
            }
        }
    }
    memcpy(sCanvas, out, sizeof(out));
}

/* The canvas into slot's art and palette. */
static void Commit(int slot)
{
    u8 art[16][16];
    u16 pal[16];
    char used[16];
    int n = 1;
    int x;
    int y;

    memset(art, 0, sizeof(art));
    memset(pal, 0, sizeof(pal));
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            char c = sCanvas[y][x];
            int i;
            size_t k;

            if (c == 0) {
                continue;
            }
            for (i = 1; i < n && used[i] != c; i++) {
            }
            if (i == n && n < 16) {
                used[n] = c;
                for (k = 0; k < sizeof(sColours) / sizeof(sColours[0]); k++) {
                    if (sColours[k].name == c) {
                        pal[n] = Bgr555((int)k);
                    }
                }
                n++;
            }
            art[y][x] = (u8)(i < 16 ? i : 1);
        }
    }
    memcpy(gHostPrompts.art[slot], art, sizeof(art));
    memcpy(gHostPrompts.pal[slot], pal, sizeof(pal));
    gHostPrompts.on[slot] = 1;
}

/* A, B are 8x16 OBJs whose lower half is the glyph; L, R 16x8. */
static int IsFace(int slot)
{
    return slot < 2;
}

/* --- What a binding looks like ------------------------------------------- */

/* SDL's game controller buttons and axes (SDL_gamecontroller.h). */
enum {
    PAD_A,
    PAD_B,
    PAD_X,
    PAD_Y,
    PAD_BACK,
    PAD_GUIDE,
    PAD_START,
    PAD_LSTICK,
    PAD_RSTICK,
    PAD_LSHOULDER,
    PAD_RSHOULDER,
};
enum {
    AXIS_TRIGGERLEFT = 4,
    AXIS_TRIGGERRIGHT = 5,
};

/* The last picture made for each slot, so a frame's call that changes
 * nothing makes nothing. */
static char sLast[4][24];

static int Same(int slot, const char *key)
{
    if (strcmp(sLast[slot], key) == 0) {
        return 1;
    }
    snprintf(sLast[slot], sizeof(sLast[slot]), "%s", key);
    return 0;
}

void HostPromptOff(int slot)
{
    if (!Same(slot, "")) {
        gHostPrompts.on[slot] = 0;
    }
}

/* A face button: the circle for the face slots, centred in the
 * shoulder slots' pill space. */
static void Face(int slot, int style, int face, int labels)
{
    int x = IsFace(slot) ? 0 : 3;
    int y = IsFace(slot) ? 8 : 0;

    if (style == HOST_PROMPTS_PLAYSTATION) {
        /* SDL's A, B, X, Y are the bottom, right, left and top buttons:
         * cross, circle, square, triangle. */
        static const int symbols[4] = { 0, 1, 2, 3 };

        Circle(x, y, 'd', 'e');
        Symbol(x, y, symbols[face]);
    } else if (style == HOST_PROMPTS_NINTENDO) {
        /* By their labels: SDL names a Nintendo pad's buttons by their
         * labels (SDL's default), so the names are the labels; another
         * pad shown in this style is named by place (bottom B, right A,
         * left Y, top X). */
        static const char *const byPlace[4] = { "B", "A", "Y", "X" };
        static const char *const byLabel[4] = { "A", "B", "X", "Y" };

        Circle(x, y, 'd', 'e');
        Text(x + 2, y + 1, labels ? byLabel[face] : byPlace[face], 'w');
    } else {
        static const char body[4] = { 'G', 'R', 'B', 'Y' };
        static const char *const names[4] = { "A", "B", "X", "Y" };

        Circle(x, y, body[face], 0);
        Text(x + 2, y + 1, names[face], face == 3 ? 'n' : 'w');
    }
}

/* A shoulder button or trigger: a dark pill with its name. */
static void Pill(const char *name)
{
    Box(0, 0, 14, 'd', 'e', 0);
    Text((14 - TextWidth(name)) / 2, 1, name, 'w');
}

int HostPromptForPad(int slot, int style, int binding, int labels)
{
    char key[24];
    int face = -1;
    int side = -1; /* 0 left, 1 right */
    int trigger = 0;
    int menu = 0; /* the Start button: the menu one */

    if (HOST_PAD_IS_BUTTON(binding)) {
        int b = binding & 0xFF;

        if (b <= PAD_Y) {
            face = b;
        } else if (b == PAD_LSHOULDER || b == PAD_RSHOULDER) {
            side = b == PAD_RSHOULDER;
        } else if (b == PAD_START) {
            menu = 1;
        }
    } else if (HOST_PAD_IS_AXIS(binding)) {
        int a = (binding & 0xFF) >> 1;

        if ((binding & 1) && (a == AXIS_TRIGGERLEFT || a == AXIS_TRIGGERRIGHT)) {
            side = a == AXIS_TRIGGERRIGHT;
            trigger = 1;
        }
    }
    if (face < 0 && ((side < 0 && !menu) || IsFace(slot))) {
        return 0;
    }
    snprintf(key, sizeof(key), "p%d %d %d %d %d %d", style, face, side, trigger, labels, menu);
    if (Same(slot, key)) {
        return 1;
    }
    memset(sCanvas, 0, sizeof(sCanvas));
    if (face >= 0) {
        Face(slot, style, face, labels);
    } else if (menu) {
        /* the menu button: three lines (Xbox's Menu, PlayStation's
         * Options), or a plus (Nintendo's +), on a dark pill */
        int r;

        Box(0, 0, 14, 'd', 'e', 0);
        if (style == HOST_PROMPTS_NINTENDO) {
            for (r = 1; r <= 5; r++) {
                Put(4 + r - 1, 3, 'w');
                Put(6, r, 'w');
            }
        } else {
            for (r = 4; r <= 9; r++) {
                Put(r, 1, 'w');
                Put(r, 3, 'w');
                Put(r, 5, 'w');
            }
        }
    } else {
        static const char *const names[3][2][2] = {
            { { "LB", "RB" }, { "LT", "RT" } }, /* Xbox */
            { { "L1", "R1" }, { "L2", "R2" } }, /* PlayStation */
            { { "L", "R" }, { "ZL", "ZR" } },   /* Nintendo */
        };
        int s = style == HOST_PROMPTS_PLAYSTATION ? 1 : style == HOST_PROMPTS_NINTENDO ? 2 : 0;

        Pill(names[s][trigger][side]);
    }
    Shadow();
    Commit(slot);
    return 1;
}

/* A key's label from SDL's name for it: one character as it is, a few
 * well-known keys shortened to three letters. */
static int KeyLabel(const char *name, char *out, size_t size)
{
    static const struct {
        const char *name;
        const char *label;
    } shortNames[] = {
        { "Space", "SPC" },        { "Return", "ENT" },     { "Enter", "ENT" },
        { "Keypad Enter", "ENT" }, { "Left Shift", "SHF" }, { "Right Shift", "SHF" },
        { "Left Ctrl", "CTL" },    { "Right Ctrl", "CTL" }, { "Left Alt", "ALT" },
        { "Right Alt", "ALT" },    { "Tab", "TAB" },        { "Backspace", "BKS" },
        { "Escape", "ESC" },       { "CapsLock", "CAP" },   { "Delete", "DEL" },
        { "Insert", "INS" },       { "Home", "HOM" },       { "End", "END" },
        { "PageUp", "PGU" },       { "PageDown", "PGD" },
    };
    size_t i;

    if (name == NULL || name[0] == 0) {
        return 0;
    }
    if (strncmp(name, "Keypad ", 7) == 0 && name[7] != 0 && name[8] == 0) {
        name += 7;
    }
    if (name[1] == 0) {
        if (Glyph35(name[0]) == NULL) {
            return 0;
        }
        snprintf(out, size, "%c", name[0]);
        return 1;
    }
    if (name[0] == 'F' && name[1] >= '1' && name[1] <= '9' && strlen(name) <= 3) {
        snprintf(out, size, "%s", name);
        return 1;
    }
    for (i = 0; i < sizeof(shortNames) / sizeof(shortNames[0]); i++) {
        if (strcmp(name, shortNames[i].name) == 0) {
            snprintf(out, size, "%s", shortNames[i].label);
            return 1;
        }
    }
    return 0;
}

int HostPromptForKey(int slot, const char *name)
{
    char label[8];
    char key[24];
    int w;

    if (!KeyLabel(name, label, sizeof(label)) || (IsFace(slot) && strlen(label) > 1)) {
        return 0;
    }
    snprintf(key, sizeof(key), "k%s", label);
    if (Same(slot, key)) {
        return 1;
    }
    memset(sCanvas, 0, sizeof(sCanvas));
    if (IsFace(slot)) {
        Box(0, 8, 7, 'l', 'm', 'm');
        Text(2, 9, label, 'n');
    } else {
        w = TextWidth(label) + 4;
        if (w < 9) {
            w = 9;
        }
        Box((14 - w) / 2, 0, w, 'l', 'm', 'm');
        Text((14 - w) / 2 + (w - TextWidth(label)) / 2, 1, label, 'n');
    }
    Shadow();
    Commit(slot);
    return 1;
}

/* prompt_hook.cpp: the font drew a button glyph this frame. */
void HostPromptGlyphDrawn(u32 firstTile, u32 palette)
{
    static int sEnvStyle = -1;

    gHostPrompts.tile = firstTile;
    gHostPrompts.palette = palette;
    gHostPrompts.frame = HostFrame();
    if (sEnvStyle < 0) {
        /* Headless runs (no SDL to pick a style): HOST_PROMPTS, with the
         * default pad bindings (SDL's B, A, the shoulders). */
        const char *e = getenv("HOST_PROMPTS");
        static const int defaults[4] = { HOST_PAD_BUTTON(PAD_B), HOST_PAD_BUTTON(PAD_A),
                                         HOST_PAD_BUTTON(PAD_LSHOULDER),
                                         HOST_PAD_BUTTON(PAD_RSHOULDER) };
        static const char *const keys[4] = { "X", "Z", "A", "S" };
        int s;

        sEnvStyle = 0;
        if (e != NULL) {
            sEnvStyle = strcmp(e, "xbox") == 0          ? HOST_PROMPTS_XBOX
                        : strcmp(e, "playstation") == 0 ? HOST_PROMPTS_PLAYSTATION
                        : strcmp(e, "nintendo") == 0    ? HOST_PROMPTS_NINTENDO
                        : strcmp(e, "keyboard") == 0    ? HOST_PROMPTS_KEYBOARD
                                                        : 0;
        }
        for (s = 0; s < 4 && sEnvStyle > 0; s++) {
            if (sEnvStyle == HOST_PROMPTS_KEYBOARD) {
                HostPromptForKey(s, keys[s]);
            } else {
                HostPromptForPad(s, sEnvStyle, defaults[s], 1);
            }
        }
    }
}

/* --- "start" -------------------------------------------------------------

 * Two of the game's texts say "start", as a word, in every language: 0x2E
 * ("push start to continue": the power dialogs and the save menu's
 * messages) and 0x2F (the warp room's "start:  load-save"). They say the
 * player's Start instead (sdl.c chooses how): gHostStartPrompt, a key's
 * name in the game's font, or else the pad's Start button, drawn
 * (StartGlyph) in the place of the font's L or R glyph: whichever the
 * screen's other text doesn't use. Only the power dialogs' descriptions
 * (0x40-0x43) use either (turbo run's says L, body slam's R), and a
 * dialog fetches its description before it draws 0x2E. The link wraps
 * GetUiText. */
char gHostStartPrompt[16]; /* a key's name, or "" */
/* The phone with no controller (stream.c): the warp room's hint says to
 * tap it (mouse_hooks.cpp makes a tap on it Start), in each language. */
int gHostTapHint;
static const char *const sTapHints[6] = {
    "tap:  load-save",         /* English */
    "touchez: charg-sauveg",   /* French */
    "tippen: laden-speichern", /* German */
    "toca: cargar-grabar",     /* Spanish */
    "tocca: carica-salva",     /* Italian */
    "tik: laden-opslaan",      /* Dutch */
};
extern s32 gLanguage;
int gHostStartStyle; /* else a pad style, or 0 */
int gHostStartLabels;

static int StartGlyph(int slot)
{
    int s;

    for (s = 0; s < HOST_BIND_SLOTS && gHostStartStyle != 0; s++) {
        if (HostPromptForPad(slot, gHostStartStyle, gHostSettings.pad[HOST_BIND_START][s],
                             gHostStartLabels)) {
            return 1;
        }
    }
    return 0;
}

/* Headless runs (no SDL to choose): HOST_PROMPTS, as for the A, B, L
 * and R glyphs, with Start on its usual key and button. */
static void StartFromEnv(void)
{
    static int sDone;
    const char *e;

    if (sDone || !gHostOptions.headless) {
        return;
    }
    sDone = 1;
    e = getenv("HOST_PROMPTS");
    if (e == NULL) {
        return;
    }
    if (strcmp(e, "keyboard") == 0) {
        snprintf(gHostStartPrompt, sizeof(gHostStartPrompt), "%s", "enter");
        return;
    }
    gHostStartStyle = strcmp(e, "xbox") == 0          ? HOST_PROMPTS_XBOX
                      : strcmp(e, "playstation") == 0 ? HOST_PROMPTS_PLAYSTATION
                      : strcmp(e, "nintendo") == 0    ? HOST_PROMPTS_NINTENDO
                                                      : 0;
    gHostStartLabels = 1;
    gHostSettings.pad[HOST_BIND_START][0] = HOST_PAD_BUTTON(PAD_START);
}

const u8 *__real_GetUiText(s32 index);

const u8 *__wrap_GetUiText(s32 index)
{
    static u8 buf[96];
    static int sFree = 2; /* the glyph slot the screen leaves free */
    const u8 *text = __real_GetUiText(index);
    const char *t = (const char *)text;
    const char *at;
    const char *word;

    if (text == NULL) {
        return text;
    }
    if (index >= 0x40 && index <= 0x43) {
        sFree = strchr(t, 'L') != NULL ? 3 : 2;
        return text;
    }
    if (index != 0x2E && index != 0x2F) {
        return text;
    }
    if (index == 0x2F && gHostTapHint && gLanguage >= 0 && gLanguage < 6) {
        return (const u8 *)sTapHints[gLanguage];
    }
    StartFromEnv();
    for (at = strstr(t, "start"); at != NULL; at = strstr(at + 1, "start")) {
        if ((at == t || !isalpha((unsigned char)at[-1])) && !isalpha((unsigned char)at[5])) {
            break;
        }
    }
    if (at == NULL) {
        return text;
    }
    if (gHostStartPrompt[0] != 0) {
        word = gHostStartPrompt;
    } else if (StartGlyph(sFree)) {
        word = sFree == 3 ? "R" : "L";
    } else {
        return text;
    }
    snprintf((char *)buf, sizeof(buf), "%.*s%s%s", (int)(at - t), t, word, at + 5);
    return buf;
}

#ifdef HOST_ANDROID
/* The Android app (stream.c): the glyphs as `style` shows the GBA's
 * buttons on the phone's controllers (phone.c's bindings), each frame;
 * HOST_PROMPTS_GBA, the touch controls' (the game's own). */
void HostPromptsPhone(int style)
{
    static const int binds[4] = { HOST_BIND_A, HOST_BIND_B, HOST_BIND_L, HOST_BIND_R };
    int s;

    gHostStartPrompt[0] = 0;
    gHostStartStyle = 0;
    if (style == HOST_PROMPTS_GBA || style == HOST_PROMPTS_AUTO || style == HOST_PROMPTS_KEYBOARD) {
        for (s = 0; s < 4; s++) {
            HostPromptOff(s);
        }
        return;
    }
    for (s = 0; s < 4; s++) {
        if (!HostPromptForPad(s, style, gHostSettings.pad[binds[s]][0], 1)) {
            HostPromptOff(s);
        }
    }
    gHostStartStyle = style;
    gHostStartLabels = 1;
}
#endif
