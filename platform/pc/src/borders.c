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

#ifdef HOST_SDL

#include <stdlib.h>
#include <string.h>

#include <SDL.h>

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

/* A bar: its texture and what it was made for. */
struct Bar {
    SDL_Texture *tex;
    int w;
    int h;
    int level;
    int side;
};

static struct Bar sBars[2];

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
static void Wall(u32 *c, int w, int h, int innerRight)
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
    for (y = 0; y < h; y++) {
        int ay = 64 + y % 87;

        for (x = 0; x < 9 && x < w; x++) {
            int cx = innerRight ? w - 1 - x : x;

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

static void MakeBar(struct Bar *bar, SDL_Renderer *r, int w, int h, int level, int side)
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

    if (c == NULL) {
        return;
    }
    Wall(c, w, h, innerRight);
    if (w >= 75) {
        if (side == 0 && level >= 0 && level < gUiLevelCount && gUiLevelPicture[level] >= 0) {
            Porthole(c, w, h, cx, cy, gUiLevelPicture[level]);
            Name(c, w, h, cx, cy + 44, gUiLevelNames[level]);
        } else if (side == 0 && level >= 0 && level < gUiLevelCount) {
            Portrait(c, w, h, cx, cy);
            Name(c, w, h, cx, cy + 44, gUiLevelNames[level]);
        } else {
            Portrait(c, w, h, cx, cy);
        }
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
    bar->w = w;
    bar->h = h;
    bar->level = level;
    bar->side = side;
}

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
        if (sBars[0].tex == NULL || sBars[0].w != w || sBars[0].h != h || sBars[0].side != 2) {
            MakeBar(&sBars[0], r, w, h, -1, 2);
        }
        top.x = 0;
        top.y = dst.y - h * unit;
        top.w = w * unit;
        top.h = h * unit;
        bottom = top;
        bottom.y = dst.y + dst.h;
        if (sBars[0].tex != NULL) {
            SDL_RenderCopy(r, sBars[0].tex, NULL, &top);
            SDL_RenderCopyEx(r, sBars[0].tex, NULL, &bottom, 0, NULL, SDL_FLIP_VERTICAL);
        }
        return;
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
        if (sBars[side].tex == NULL || sBars[side].w != w || sBars[side].h != h ||
            sBars[side].level != gHostPlayingLevel || sBars[side].side != side) {
            MakeBar(&sBars[side], r, w, h, gHostPlayingLevel, side);
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
