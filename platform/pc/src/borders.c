/* The themed borders (docs/platforms.md, "Borders"): what fills the
 * window beside the picture when its shape isn't the GBA's 3:2 and the
 * Borders setting says "themed".
 *
 * A warp room's wall, from the game's own pieces (ui_assets.c): the pause
 * screen's grooved metal and bolt strips tiled down the bar, its riveted
 * plate along the edge next to the picture, and in the middle of each bar
 * a porthole: in a level the left one shows its theme's picture (the
 * level select's) with the level's name under it in the game's font,
 * the right one Crash's portrait from the pause screen; outside the
 * levels (the menus, the cutscenes) both show the portrait. The bars are
 * drawn at a whole multiple of their own pixels (`unit`), chosen so that
 * a porthole fits across them, nearest-neighbour like the picture, and
 * made again only when the window's size or the level changes. Borders
 * above and below the picture (a tall window) are the metal alone. */

#if defined(HOST_SDL) || defined(HOST_ANDROID)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HOST_SDL
#include <SDL.h>
#endif

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

extern const unsigned char gUiPauseArt[160][240];
extern const unsigned short gUiPausePalette[256];
extern const int gUiLevelCount;
extern const char *const gUiLevelNames[];
extern const signed char gUiLevelPicture[];
extern const unsigned char gUiPictures[][64 * 64];
extern const unsigned short gUiPicturePalettes[][256];
extern s32 gHostPlayingLevel;

#include "constants/progress_flags.h"

/* The pause menu's icons (ui_assets.c), in this order. */
struct UiIcon {
    int w;
    int h;
    const unsigned int *px;
};
extern const struct UiIcon gUiIcons[];
enum {
    ICON_CRYSTAL,
    ICON_GEM_CLEAR,
    ICON_GEM_RED,
    ICON_GEM_GREEN,
    ICON_GEM_BLUE,
    ICON_GEM_YELLOW,
    ICON_RELIC_PLATINUM,
    ICON_RELIC_GOLD,
    ICON_RELIC_SAPPHIRE,
    ICON_POWER_SUPER_BODY_SLAM,
    ICON_POWER_DOUBLE_JUMP,
    ICON_POWER_TORNADO_SPIN,
    ICON_POWER_TURBO_RUN,
    ICON_HUD_LIVES,
    ICON_HUD_CRATE,
    ICON_HUD_WUMPA,
    ICON_HUD_DIGIT0, /* the HUD's large digits, 0-9 */
    ICON_HUD_SLASH = ICON_HUD_DIGIT0 + 10,
    ICON_HUD_COLON,
    ICON_HUD_DOT,
    ICON_HUD_CRATE_SMALL, /* a third of the crate */
    ICON_HUD_SMALL0,      /* its small digits, 0-9 */
};

/* Interface's HUD in "borders": set while the left border has the HUD
 * (hud_hook.cpp takes it out of the picture only then). */
extern int gHostHudInBorder;

/* The player's progress: the level state's progress block (its first
 * field, game_progress.hpp), counted by the game's own functions, as the
 * pause menu's pages count it (pause_menu_pages_init.cpp). */
extern void *gLevelState;
s32 CountCrystals(const void *progress);
s32 CountGems(const void *progress);
s32 CountClearGems(const void *progress);
s32 CountRelics(const void *progress);
s32 CountSapphireRelics(const void *progress);
s32 CountGoldRelics(const void *progress);
s32 CountPlatinumRelics(const void *progress);
s32 GetCompletionPercent(const void *progress);

struct Stats {
    int valid;
    int percent;
    int crystals;
    int gems;
    int clearGems;
    int relics;
    int sapphire;
    int gold;
    int platinum;
    int flags;
};

extern int gHostStatsShown; /* level_pick.c */

static void ReadStats(struct Stats *st)
{
    const u8 *progress = gLevelState;

    memset(st, 0, sizeof(*st));
    /* Only once a game is under way: from the first warp room on, until
     * the title screen again (level_pick.c). Before it the progress is
     * the title's, not a game's. */
    if (progress == NULL || !gHostStatsShown) {
        return;
    }
    st->valid = 1;
    st->percent = GetCompletionPercent(progress);
    st->crystals = CountCrystals(progress);
    st->gems = CountGems(progress);
    st->clearGems = CountClearGems(progress);
    st->relics = CountRelics(progress);
    st->sapphire = CountSapphireRelics(progress);
    st->gold = CountGoldRelics(progress);
    st->platinum = CountPlatinumRelics(progress);
    st->flags = progress[2];
}

/* A bar: its texture and what it was made for. */
struct Bar {
#ifdef HOST_SDL
    SDL_Texture *tex;
#endif
    int made;
    int w;
    int h;
    int level;
    int side;
    struct Stats stats;
    struct HostHud hud; /* the left bar's HUD panel's numbers (frame 0) */
    int hudShown;
    int pad; /* the input display's setting it was made for */
};

static struct Stats sStats;

static struct Bar sBars[3]; /* left, right, above and below */

static inline u32 Color15(u32 c)
{
    u32 r = c & 0x1F;
    u32 g = (c >> 5) & 0x1F;
    u32 b = (c >> 10) & 0x1F;

    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return 0xFF000000 | r << 16 | g << 8 | b;
}

static inline u32 Art(int x, int y)
{
    return Color15(gUiPausePalette[gUiPauseArt[y][x]]);
}

static u32 Dark(u32 c, int num, int den)
{
    u32 r = ((c >> 16) & 0xFF) * num / den;
    u32 g = ((c >> 8) & 0xFF) * num / den;
    u32 b = (c & 0xFF) * num / den;

    return 0xFF000000 | r << 16 | g << 8 | b;
}

/* The wall: the title band's grooved metal (art rows 8-31) with the
 * bolt strip (rows 0-7) between, every 32 rows; the riveted plate (the
 * pause box's left edge, art columns 0-8 of rows 64-150) down the side
 * that meets the picture. The wall is a little darker than the art, so
 * that the picture stays in front. */
enum {
    PLATE_RIGHT,  /* the left bar: the plate down its right side */
    PLATE_LEFT,   /* the right bar */
    PLATE_BOTTOM, /* above the picture: along its bottom (flipped, below) */
};

static void Wall(u32 *c, int w, int h, int plate)
{
    int x;
    int y;

    for (y = 0; y < h; y++) {
        int r = y % 32;

        for (x = 0; x < w; x++) {
            u32 px = r < 8 ? Art((x + 4) % 240, r) : Art(x % 48, 8 + (r - 8));

            c[y * w + x] = Dark(px, 3, 4);
        }
    }
    if (plate == PLATE_BOTTOM) {
        /* The same plate turned on its side: its first column (the one
         * meeting the picture) along the bottom row. */
        for (x = 0; x < w; x++) {
            int ay = 64 + x % 87;

            for (y = 0; y < 9 && y < h; y++) {
                c[(h - 1 - y) * w + x] = Art(y, ay);
            }
        }
        return;
    }
    for (y = 0; y < h; y++) {
        int ay = 64 + y % 87;

        for (x = 0; x < 9 && x < w; x++) {
            int cx = plate == PLATE_RIGHT ? w - 1 - x : x;

            c[y * w + cx] = Art(x, ay);
        }
    }
}

/* A round frame of radius `r` around (cx, cy): dark outside, the art's
 * light steel inside, as the pause screen's monitor rim. */
static void Ring(u32 *c, int w, int h, int cx, int cy, int r)
{
    int x;
    int y;

    for (y = cy - r - 3; y <= cy + r + 3; y++) {
        for (x = cx - r - 3; x <= cx + r + 3; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            u32 color;

            if (x < 0 || y < 0 || x >= w || y >= h) {
                continue;
            }
            if (d2 > (r + 3) * (r + 3) || d2 < r * r) {
                continue;
            }
            if (d2 >= (r + 2) * (r + 2)) {
                color = 0xFF100808;
            } else if (d2 >= (r + 1) * (r + 1)) {
                color = Color15(gUiPausePalette[gUiPauseArt[151][90]]);
            } else {
                color = 0xFF384890;
            }
            c[y * w + x] = color;
        }
    }
}

/* The level's theme picture in a porthole, centred at (cx, cy). */
static void Porthole(u32 *c, int w, int h, int cx, int cy, int picture)
{
    const unsigned char *pix = gUiPictures[picture];
    const unsigned short *pal = gUiPicturePalettes[picture];
    int x;
    int y;

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            int px = cx - 32 + x;
            int py = cy - 32 + y;
            int dx = 2 * x - 63;
            int dy = 2 * y - 63;

            if (px < 0 || py < 0 || px >= w || py >= h || dx * dx + dy * dy > 60 * 60) {
                continue;
            }
            /* The picture's transparent colour (its corners, and the odd
             * pixel at its rim) is the porthole's dark glass. */
            c[py * w + px] = pix[y * 64 + x] != 0 ? Color15(pal[pix[y * 64 + x]]) : 0xFF101830;
        }
    }
    Ring(c, w, h, cx, cy, 30);
}

/* Crash's portrait from the pause screen (its blue swirl and his head:
 * art 97-148 x 64-132), centred at (cx, cy), over the wall. */
static void Portrait(u32 *c, int w, int h, int cx, int cy)
{
    int x;
    int y;

    for (y = 64; y < 133; y++) {
        for (x = 97; x < 149; x++) {
            int px = cx - (149 - 97) / 2 + (x - 97);
            int py = cy - (133 - 64) / 2 + (y - 64);

            if (px < 0 || py < 0 || px >= w || py >= h) {
                continue;
            }
            c[py * w + px] = Art(x, y);
        }
    }
}

static void Name(u32 *c, int w, int h, int cx, int top, const char *s)
{
    char lines[3][32];
    int n = 0;
    const char *p = s;
    int i;

    /* Words to lines that fit the bar. */
    while (*p != '\0' && n < 3) {
        char line[32] = "";

        while (*p == ' ') {
            p++;
        }
        while (*p != '\0') {
            char test[64];
            int wl = (int)strcspn(p, " ");

            snprintf(test, sizeof(test), "%s%s%.*s", line, line[0] ? " " : "", wl, p);
            if (line[0] != '\0' && HostUiTextWidth(0, test) > w - 14) {
                break;
            }
            snprintf(line, sizeof(line), "%s", test);
            p += wl;
            while (*p == ' ') {
                p++;
            }
        }
        snprintf(lines[n++], sizeof(lines[0]), "%s", line);
    }
    for (i = 0; i < n; i++) {
        int tw = HostUiTextWidth(0, lines[i]);
        int x;
        int y;

        /* a dark plate under the words */
        for (y = top + 10 * i - 2; y < top + 10 * i + 9; y++) {
            for (x = cx - tw / 2 - 3; x < cx + tw / 2 + 3; x++) {
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    c[y * w + x] = Dark(c[y * w + x], 1, 3);
                }
            }
        }
        HostUiText(c, w, h, 0, cx - tw / 2, top + 10 * i, lines[i], HOST_UI_PAL_NORMAL);
    }
}

/* An icon centred at (cx, cy); `dim` for what the player hasn't got. */
static void Icon(u32 *c, int w, int h, int id, int cx, int cy, int dim)
{
    const struct UiIcon *ic = &gUiIcons[id];
    int x;
    int y;

    for (y = 0; y < ic->h; y++) {
        for (x = 0; x < ic->w; x++) {
            u32 v = ic->px[y * ic->w + x];
            int px = cx - ic->w / 2 + x;
            int py = cy - ic->h / 2 + y;

            if (v == 0 || px < 0 || py < 0 || px >= w || py >= h) {
                continue;
            }
            if (dim) {
                u32 l = (((v >> 16) & 0xFF) * 30 + ((v >> 8) & 0xFF) * 59 + (v & 0xFF) * 11) / 100;

                l = 24 + l / 4;
                v = 0xFF000000 | l << 16 | l << 8 | (l + 12);
            }
            c[py * w + px] = v;
        }
    }
}

/* A dark plate, as the pause screen's inset boxes, behind a section. */
static void Plate(u32 *c, int w, int h, int x0, int y0, int x1, int y1)
{
    int x;
    int y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (x < 0 || y < 0 || x >= w || y >= h) {
                continue;
            }
            if (y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1) {
                c[y * w + x] = 0xFF384890;
            } else {
                c[y * w + x] = Dark(c[y * w + x], 1, 4);
            }
        }
    }
}

static void Centered(u32 *c, int w, int h, int large, int cx, int y, const char *s)
{
    HostUiText(c, w, h, large, cx - HostUiTextWidth(large, s) / 2, y, s, HOST_UI_PAL_NORMAL);
}

/* The right bar's stats: the pause menu's pages at a glance. Completion,
 * the crystals, the gems (the four coloured ones grey until found), the
 * relics of each kind and the powers (grey until earned); the section
 * titles in the small font as the pause pages' titles, the numbers in
 * the large one as their counts. */
static void StatsPanel(u32 *c, int w, int h, const struct Stats *st, int reserve)
{
    int x0 = 9 + 2;
    int x1 = w - 2;
    int cx = (x0 + x1) / 2;
    int y = h / 2 - 165;
    char text[32];
    static const struct {
        int flag;
        int icon;
    } gems[4] = {
        { PROGRESS_FLAG_RED_GEM, ICON_GEM_RED },
        { PROGRESS_FLAG_GREEN_GEM, ICON_GEM_GREEN },
        { PROGRESS_FLAG_BLUE_GEM, ICON_GEM_BLUE },
        { PROGRESS_FLAG_YELLOW_GEM, ICON_GEM_YELLOW },
    };
    static const struct {
        int flag;
        int icon;
    } powers[4] = {
        { PROGRESS_FLAG_SUPER_BODY_SLAM, ICON_POWER_SUPER_BODY_SLAM },
        { PROGRESS_FLAG_DOUBLE_JUMP, ICON_POWER_DOUBLE_JUMP },
        { PROGRESS_FLAG_TORNADO_SPIN, ICON_POWER_TORNADO_SPIN },
        { PROGRESS_FLAG_TURBO_RUN, ICON_POWER_TURBO_RUN },
    };
    int i;

    /* 278: the five plates and their gaps; above the input display */
    if (y + 278 > h - reserve) {
        y = h - reserve - 278;
    }
    if (y < 4) {
        y = 4;
    }
    /* Completion */
    Plate(c, w, h, x0, y, x1, y + 30);
    Centered(c, w, h, 0, cx, y + 4, "complete");
    snprintf(text, sizeof(text), "%d%%", st->percent);
    Centered(c, w, h, 1, cx, y + 13, text);
    y += 36;
    /* Crystals */
    Plate(c, w, h, x0, y, x1, y + 38);
    Centered(c, w, h, 0, cx, y + 4, "crystals");
    Icon(c, w, h, ICON_CRYSTAL, x0 + 10, y + 25, 0);
    snprintf(text, sizeof(text), "%d/%d", st->crystals, 20);
    Centered(c, w, h, 1, cx + 7, y + 17, text);
    y += 44;
    /* Gems: the clear ones' count, then the four coloured ones in a
     * square (four in a row are wider than the bar's plate) */
    Plate(c, w, h, x0, y, x1, y + 64);
    Centered(c, w, h, 0, cx, y + 4, "gems");
    Icon(c, w, h, ICON_GEM_CLEAR, x0 + 10, y + 22, 0);
    snprintf(text, sizeof(text), "%d/%d", st->gems, 28);
    Centered(c, w, h, 1, cx + 7, y + 15, text);
    for (i = 0; i < 4; i++) {
        Icon(c, w, h, gems[i].icon, cx - 9 + 18 * (i & 1), y + 40 + 15 * (i >> 1), !(st->flags & gems[i].flag));
    }
    y += 70;
    /* Relics */
    Plate(c, w, h, x0, y, x1, y + 72);
    Centered(c, w, h, 0, cx, y + 4, "relics");
    {
        static const int icons[3] = { ICON_RELIC_SAPPHIRE, ICON_RELIC_GOLD, ICON_RELIC_PLATINUM };
        int counts[3];

        counts[0] = st->sapphire;
        counts[1] = st->gold;
        counts[2] = st->platinum;
        for (i = 0; i < 3; i++) {
            Icon(c, w, h, icons[i], cx - 20 + 20 * i, y + 26, 0);
            snprintf(text, sizeof(text), "%d", counts[i]);
            Centered(c, w, h, 0, cx - 20 + 20 * i, y + 40, text);
        }
        snprintf(text, sizeof(text), "%d/%d", st->relics, 20);
        Centered(c, w, h, 1, cx, y + 51, text);
    }
    y += 78;
    /* Powers */
    Plate(c, w, h, x0, y, x1, y + 50);
    Centered(c, w, h, 0, cx, y + 4, "powers");
    for (i = 0; i < 4; i++) {
        Icon(c, w, h, powers[i].icon, cx - 9 + 18 * (i & 1), y + 22 + 16 * (i >> 1), !(st->flags & powers[i].flag));
    }
}

/* A number in the HUD's small digits (and '/', ':', '.'), right-aligned
 * at x1, its glyphs' tops at y. */
#ifdef HOST_ANDROID
/* The phone's right bar: the touch buttons cover its lower half, so the
 * progress in one plate at the top: the completion, then the crystals,
 * gems and relics, each by its icon. */
static void CompactStats(u32 *c, int w, int h, const struct Stats *st)
{
    int x0 = 9 + 2;
    int x1 = w - 2;
    int cx = (x0 + x1) / 2;
    int y = 6;
    char text[32];
    static const int icons[3] = { ICON_CRYSTAL, ICON_GEM_CLEAR, ICON_RELIC_PLATINUM };
    static const int totals[3] = { 20, 28, 20 };
    int counts[3];
    int i;

    counts[0] = st->crystals;
    counts[1] = st->gems;
    counts[2] = st->relics;
    if (h < 110) {
        return;
    }
    Plate(c, w, h, x0, y, x1, y + 96);
    Centered(c, w, h, 0, cx, y + 4, "complete");
    snprintf(text, sizeof(text), "%d%%", st->percent);
    Centered(c, w, h, 1, cx, y + 13, text);
    for (i = 0; i < 3; i++) {
        Icon(c, w, h, icons[i], x0 + 10, y + 42 + 19 * i, 0);
        snprintf(text, sizeof(text), "%d/%d", counts[i], totals[i]);
        Centered(c, w, h, 1, cx + 7, y + 35 + 19 * i, text);
    }
}
#endif

static int GlyphOf(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ICON_HUD_SMALL0 + (ch - '0');
    }
    return ch == '/' ? ICON_HUD_SLASH : ch == ':' ? ICON_HUD_COLON : ch == '.' ? ICON_HUD_DOT : -1;
}

/* A glyph's advance: its width and a pixel; tight, no pixel, and the
 * slash (a diagonal) tucked 2 into each neighbour's empty corner. */
static int Advance(const char *p, int tight)
{
    int g = GlyphOf(*p);
    int a;

    if (g < 0) {
        return 3;
    }
    a = gUiIcons[g].w + (tight ? 0 : 1);
    if (tight && (*p == '/' || p[1] == '/')) {
        a -= 2;
    }
    return a;
}

static int HudNumberWidth(const char *s, int tight)
{
    int width = 0;

    for (; *s; s++) {
        width += Advance(s, tight);
    }
    return width;
}

static void HudNumber(u32 *c, int w, int h, int x1, int y, const char *s, int tight)
{
    const char *p;
    int x = x1 - HudNumberWidth(s, tight);

    for (p = s; *p; p++) {
        int g = GlyphOf(*p);

        if (g >= 0) {
            /* Icon centres; the glyphs sit on a common baseline (y + 13) */
            Icon(c, w, h, g, x + gUiIcons[g].w / 2, y + 13 - gUiIcons[g].h + gUiIcons[g].h / 2, 0);
        }
        x += Advance(p, tight);
    }
}

/* The HUD in the left border: lives, wumpa, crates (and the time trial's
 * clock), with the HUD's own icons and digits, on a plate under the
 * level's name. A crate count too wide to sit beside its icon
 * ("112/134") gets a smaller crate and its digits closer together. */
struct HudLayout {
    char text[4][24];
    int tight[4]; /* the row's widest doesn't fit beside its icon: the
                   * icon a third and the digits close together */
    int rows;
    int height;
};

static const int sHudIcons[4] = { ICON_HUD_LIVES, ICON_HUD_WUMPA, ICON_HUD_CRATE, -1 };

static void LayOutHud(struct HudLayout *l, int x0, int x1, const struct HostHud *hud)
{
    int i;

    l->rows = 3 + (hud->timeTrial != 0);
    l->height = 6;
    snprintf(l->text[0], sizeof(l->text[0]), "%d", (int)hud->lives);
    snprintf(l->text[1], sizeof(l->text[1]), "%d", (int)hud->wumpa);
    if (hud->crateTotal > 0) {
        snprintf(l->text[2], sizeof(l->text[2]), "%d/%d", (int)hud->crates, (int)hud->crateTotal);
    } else {
        snprintf(l->text[2], sizeof(l->text[2]), "%d", (int)hud->crates);
    }
    snprintf(l->text[3], sizeof(l->text[3]), "%d:%02d.%d", (int)hud->minutes, (int)hud->seconds, (int)hud->tenths);
    for (i = 0; i < l->rows; i++) {
        /* judged by the widest it can get, so the row keeps its look
         * through the level: 99 lives, 100 wumpa, every crate */
        char widest[24];

        if (i == 2 && hud->crateTotal > 0) {
            snprintf(widest, sizeof(widest), "%d/%d", (int)hud->crateTotal, (int)hud->crateTotal);
        } else {
            snprintf(widest, sizeof(widest), "%s", i == 0 ? "99" : i == 1 ? "100" : l->text[i]);
        }
        l->tight[i] = sHudIcons[i] >= 0 && HudNumberWidth(widest, 0) > (x1 - 4) - (x0 + 22);
        l->height += 20;
    }
}

static void HudPanel(u32 *c, int w, int h, int x0, int x1, int y, const struct HostHud *hud)
{
    struct HudLayout l;
    int cx;
    int ry;
    int i;

    LayOutHud(&l, x0, x1, hud);
    Plate(c, w, h, x0, y, x1, y + l.height);
    ry = y + 4;
    /* The icons' column: the middle of the room beside the numbers; with
     * a tight row, nearer the plate's edge, for the small crate there and
     * the others with it. */
    for (i = 0, cx = x0 + 12; i < l.rows; i++) {
        if (l.tight[i]) {
            cx = x0 + 8;
        }
    }
    for (i = 0; i < l.rows; i++) {
        if (l.tight[i]) {
            Icon(c, w, h, ICON_HUD_CRATE_SMALL, cx, ry + 8, 0);
            HudNumber(c, w, h, x1 - 2, ry + 1, l.text[i], 1);
        } else {
            if (sHudIcons[i] >= 0) {
                Icon(c, w, h, sHudIcons[i], cx, ry + 8, 0);
            }
            HudNumber(c, w, h, x1 - 4, ry + 1, l.text[i], 0);
        }
        ry += 20;
    }
}

static int HudInBorder(void)
{
    return gHostSettings.hud == HOST_HUD_BORDERS && gHostPlayingLevel >= 0 && gHostHud.frame != 0;
}

/* A bar's pixels (ARGB, w x h of its own pixels), malloc'd; NULL if out
 * of memory. */
static u32 *BarPixels(int w, int h, int level, int side)
{
    u32 *c = malloc((size_t)w * h * 4);
    int innerRight = side == 0;
    int cx = innerRight ? (w - 9) / 2 : 9 + (w - 9) / 2;

    /* the porthole's rim (radius 33) stays inside the bar */
    if (innerRight && cx < 34) {
        cx = 34;
    }
    if (!innerRight && cx > w - 35) {
        cx = w - 35;
    }
    int cy = h * 2 / 5;
    /* the input display's room at the bottom of its border (sdl.c) */
    int reserve = side < 2 && gHostSettings.inputDisplay == side + 1 ? HOST_PAD_ART_H + 8 : 0;

#ifdef HOST_ANDROID
    /* the phone's touch buttons are over the bars' lower half */
    if (side < 2) {
        reserve = h / 2;
    }
#endif

    if (c == NULL) {
        return NULL;
    }
    /* the porthole (or portrait) and the name above it */
    if (cy + 56 > h - reserve) {
        cy = h - reserve - 56;
    }
    /* The left bar with the HUD: the porthole, the name and the panel
     * (from cy - 33 to cy + 64 + its height) kept inside the bar. */
    if (side == 0 && HudInBorder()) {
        struct HudLayout l;

        LayOutHud(&l, 2, w - 11, &gHostHud);
        if (cy > h - 4 - 64 - l.height - reserve) {
            cy = h - 4 - 64 - l.height - reserve;
        }
        if (cy < 37) {
            cy = 37;
        }
    }
    Wall(c, w, h, side == 0 ? PLATE_RIGHT : side == 1 ? PLATE_LEFT : PLATE_BOTTOM);
    if (w >= 75 && side < 2) {
        if (side == 0 && level >= 0 && level < gUiLevelCount && gUiLevelPicture[level] >= 0) {
            Porthole(c, w, h, cx, cy, gUiLevelPicture[level]);
            Name(c, w, h, cx, cy + 44, gUiLevelNames[level]);
            if (HudInBorder()) {
                HudPanel(c, w, h, 2, w - 11, cy + 64, &gHostHud);
            }
        } else if (side == 0 && level >= 0 && level < gUiLevelCount) {
            Portrait(c, w, h, cx, cy);
            Name(c, w, h, cx, cy + 44, gUiLevelNames[level]);
            if (HudInBorder()) {
                HudPanel(c, w, h, 2, w - 11, cy + 64, &gHostHud);
            }
        } else if (side == 1 && sStats.valid) {
#ifdef HOST_ANDROID
            CompactStats(c, w, h, &sStats);
#else
            StatsPanel(c, w, h, &sStats, reserve);
#endif
        } else {
            Portrait(c, w, h, cx, cy);
        }
    }
    return c;
}

/* What a bar was made for (to know when to make it again). */
static void NoteBar(struct Bar *bar, int w, int h, int level, int side)
{
    bar->made = 1;
    bar->w = w;
    bar->h = h;
    bar->level = level;
    bar->side = side;
    bar->stats = sStats;
    bar->hud = gHostHud;
    bar->hud.frame = 0;
    bar->hudShown = HudInBorder();
    bar->pad = gHostSettings.inputDisplay;
}

static int BarStale(const struct Bar *bar, int w, int h, int side);

#ifdef HOST_SDL
static void MakeBar(struct Bar *bar, SDL_Renderer *r, int w, int h, int level, int side)
{
    u32 *c = BarPixels(w, h, level, side);

    if (c == NULL) {
        return;
    }
    if (bar->tex != NULL) {
        SDL_DestroyTexture(bar->tex);
    }
    bar->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (bar->tex != NULL) {
        SDL_UpdateTexture(bar->tex, NULL, c, w * 4);
        SDL_SetTextureScaleMode(bar->tex, SDL_ScaleModeNearest);
    }
    free(c);
    NoteBar(bar, w, h, level, side);
}

/* --discord-assets DIR: the Discord application's art assets (discord.c),
 * 512x512 PNGs named as the assets: each world's porthole and Crash's
 * portrait, drawn as in the borders, 7 times their pixels, on nothing. */
static void WriteAsset(const char *dir, const char *name, const u32 *c, int w, int h)
{
    static u32 out[512 * 512];
    char path[1024];
    int k = 7;
    int ox = (512 - w * k) / 2;
    int oy = (512 - h * k) / 2;
    int x;
    int y;

    memset(out, 0, sizeof(out));
    for (y = 0; y < h * k; y++) {
        for (x = 0; x < w * k; x++) {
            out[(oy + y) * 512 + ox + x] = c[(y / k) * w + x / k];
        }
    }
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    if (!HostWritePngAlpha(path, out, 512, 512)) {
        fprintf(stderr, "can't write %s\n", path);
    }
}

void HostBordersWriteDiscordAssets(const char *dir)
{
    static const char *const names[10] = {
        "world_snow", "world_jungle", "world_sewer", "world_underwater", "world_sky",
        "world_space", "boss_tiny", "boss_dingodile", "boss_ngin", "boss_cortex",
    };
    u32 c[72 * 72];
    u32 p[52 * 69];
    int i;

    for (i = 0; i < 10; i++) {
        memset(c, 0, sizeof(c));
        Porthole(c, 72, 72, 36, 36, i);
        WriteAsset(dir, names[i], c, 72, 72);
    }
    /* Crash: his portrait in a porthole too (Discord shows the small
     * picture in a circle). */
    memset(p, 0, sizeof(p));
    Portrait(p, 52, 69, 26, 34);
    memset(c, 0, sizeof(c));
    {
        int x;
        int y;

        for (y = 0; y < 72; y++) {
            for (x = 0; x < 72; x++) {
                int dx = 2 * x - 71;
                int dy = 2 * y - 71;
                int sx = x - 36 + 26;
                int sy = y - 36 + 33;

                if (dx * dx + dy * dy > 60 * 60) {
                    continue;
                }
                c[y * 72 + x] = sx >= 0 && sx < 52 && sy >= 0 && sy < 69 ? p[sy * 52 + sx] : 0xFF101830;
            }
        }
    }
    Ring(c, 72, 72, 36, 36, 30);
    WriteAsset(dir, "crash", c, 72, 72);
    printf("discord assets: %s (upload them to the application's Rich Presence art assets)\n", dir);
}

#endif /* HOST_SDL */

static int HudChanged(const struct Bar *bar)
{
    struct HostHud now = gHostHud;

    now.frame = 0;
    if (bar->hudShown != HudInBorder()) {
        return 1;
    }
    return bar->hudShown && memcmp(&bar->hud, &now, sizeof(now)) != 0;
}

/* A side bar (0 left, 1 right) made for something else than now. */
static int BarStale(const struct Bar *bar, int w, int h, int side)
{
    return !bar->made || bar->w != w || bar->h != h || bar->level != gHostPlayingLevel || bar->side != side ||
           bar->pad != gHostSettings.inputDisplay ||
           (side == 1 && memcmp(&bar->stats, &sStats, sizeof(sStats)) != 0) || (side == 0 && HudChanged(bar));
}

#ifdef HOST_SDL
void HostBordersDrawThemed(SDL_Renderer *r, SDL_Rect dst, int ww, int wh)
{
    int side;

    if (dst.x <= 0) {
        /* Above and below: the wall, across. */
        int unit = wh - dst.h > 0 ? ((dst.h / HOST_SCREEN_HEIGHT + 1) / 2 > 1 ? (dst.h / HOST_SCREEN_HEIGHT + 1) / 2 : 1) : 1;
        int w = (ww + unit - 1) / unit;
        int h = (dst.y + unit - 1) / unit;
        SDL_Rect top;
        SDL_Rect bottom;

        if (h <= 0) {
            return;
        }
        if (sBars[2].tex == NULL || sBars[2].w != w || sBars[2].h != h || sBars[2].side != 2) {
            MakeBar(&sBars[2], r, w, h, -1, 2);
        }
        top.x = 0;
        top.y = dst.y - h * unit;
        top.w = w * unit;
        top.h = h * unit;
        bottom = top;
        bottom.y = dst.y + dst.h;
        if (sBars[2].tex != NULL) {
            SDL_RenderCopy(r, sBars[2].tex, NULL, &top);
            SDL_RenderCopyEx(r, sBars[2].tex, NULL, &bottom, 0, NULL, SDL_FLIP_VERTICAL);
        }
        return;
    }
    ReadStats(&sStats);
    gHostHudInBorder = 0;
    /* Above and below the picture too (integer scaling leaves both): the
     * wall across, at the side bars' pixel size, with the plate along the
     * picture's edge; the side bars, drawn after, cover its ends. */
    if (dst.y > 0) {
        int unit = dst.x / 75 > 1 ? dst.x / 75 : 1;
        int w = (dst.w + unit - 1) / unit;
        int h = (dst.y + unit - 1) / unit;
        SDL_Rect top;
        SDL_Rect bottom;

        if (sBars[2].tex == NULL || sBars[2].w != w || sBars[2].h != h || sBars[2].side != 2) {
            MakeBar(&sBars[2], r, w, h, -1, 2);
        }
        top.x = dst.x + (dst.w - w * unit) / 2;
        top.y = dst.y - h * unit;
        top.w = w * unit;
        top.h = h * unit;
        bottom = top;
        bottom.y = dst.y + dst.h;
        if (sBars[2].tex != NULL) {
            SDL_RenderCopy(r, sBars[2].tex, NULL, &top);
            SDL_RenderCopyEx(r, sBars[2].tex, NULL, &bottom, 0, NULL, SDL_FLIP_VERTICAL);
        }
    }
    for (side = 0; side < 2; side++) {
        int barW = side == 0 ? dst.x : ww - (dst.x + dst.w);
        int unit = barW / 75 > 1 ? barW / 75 : 1;
        int w = (barW + unit - 1) / unit;
        int h = (wh + unit - 1) / unit;
        SDL_Rect to;

        if (barW <= 0) {
            continue;
        }
        if (sBars[side].tex == NULL || BarStale(&sBars[side], w, h, side)) {
            MakeBar(&sBars[side], r, w, h, gHostPlayingLevel, side);
        }
        if (side == 0 && w >= 75 && HudInBorder()) {
            gHostHudInBorder = 1;
        }
        /* Each bar meets the picture's edge with its whole pixels. */
        to.w = w * unit;
        to.h = h * unit;
        to.x = side == 0 ? dst.x - to.w : dst.x + dst.w;
        to.y = (wh - to.h) / 2;
        if (sBars[side].tex != NULL) {
            SDL_RenderCopy(r, sBars[side].tex, NULL, &to);
        }
    }
}

#endif /* HOST_SDL */

#ifdef HOST_ANDROID
/* The phone (stream.c): the bars as the window would draw them beside a
 * picture `picW` wide with `left` and `right` pixels of screen beside it
 * and `top` above it, on a screen `screenH` high; each bar that changed
 * goes to `send` (side 0 left, 1 right, 2 above, which the app also draws
 * flipped below; its pixel size `unit`), and the HUD's place is said
 * (gHostHudInBorder). */
void HostBordersPhone(int left, int right, int screenH, int picW, int top,
                      void (*send)(int side, int unit, int w, int h, const u32 *px))
{
    int side;

    gHostHudInBorder = 0;
    if (gHostSettings.borders != HOST_BORDERS_THEMED || screenH <= 0) {
        return;
    }
    ReadStats(&sStats);
    if (top > 0) {
        int unit;
        int w;
        int h;

        if (left > 0) {
            unit = left / 75 > 1 ? left / 75 : 1;
        } else {
            int k = (screenH - 2 * top) / HOST_SCREEN_HEIGHT;

            unit = (k + 1) / 2 > 1 ? (k + 1) / 2 : 1;
        }
        w = (picW + unit - 1) / unit;
        h = (top + unit - 1) / unit;
        if (!sBars[2].made || sBars[2].w != w || sBars[2].h != h || sBars[2].side != 2) {
            u32 *c = BarPixels(w, h, -1, 2);

            if (c != NULL) {
                send(2, unit, w, h, c);
                free(c);
                NoteBar(&sBars[2], w, h, -1, 2);
            }
        }
    }
    for (side = 0; side < 2; side++) {
        int barW = side == 0 ? left : right;
        int unit = barW / 75 > 1 ? barW / 75 : 1;
        int w = (barW + unit - 1) / unit;
        int h = (screenH + unit - 1) / unit;

        if (barW <= 0) {
            continue;
        }
        if (BarStale(&sBars[side], w, h, side)) {
            u32 *c = BarPixels(w, h, gHostPlayingLevel, side);

            if (c != NULL) {
                send(side, unit, w, h, c);
                free(c);
                NoteBar(&sBars[side], w, h, gHostPlayingLevel, side);
            }
        }
        if (side == 0 && w >= 75 && HudInBorder()) {
            gHostHudInBorder = 1;
        }
    }
}

/* The app's screen changed (or the bars must go again). */
void HostBordersPhoneReset(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        sBars[i].made = 0;
    }
}
#endif /* HOST_ANDROID */

#endif /* HOST_SDL || HOST_ANDROID */
