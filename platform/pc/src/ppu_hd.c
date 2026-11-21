/* The interpolation's PPU (host_interp.h, docs/platforms.md "Frame
 * interpolation"): ppu.c's picture, drawn at `scale` times the GBA's
 * resolution from a kept frame's state (registers, VRAM, palette RAM,
 * OAM) with each BG's scroll and each OBJ's position moved by a fraction
 * of a pixel (in 1/scale pixels), so that a picture between two frames
 * shows its layers between pixels.
 *
 * It is ppu.c's rules, not a resampled picture: every hi-res pixel takes
 * the texel of the GBA pixel it lies in, of each layer at that layer's
 * position, and the layers are put together per hi-res pixel as ppu.c
 * puts them together per pixel (priorities, OBJ priority passing, the
 * semi-transparent OBJs, BLDCNT's effects, the windows). With nothing
 * moved, every scale x scale block is ppu.c's pixel (--hd-check tests it).
 * The colour effects' coefficients come in 256ths (EVA * 16 for the
 * GBA's), so that a fade can be drawn between its steps; whole steps give
 * ppu.c's (and mGBA's) colours exactly.
 *
 * What the game doesn't use is drawn without moving: the affine BGs
 * unless their matrix is the identity (then they scroll like a text BG),
 * the windows (the GBA's pixel), and mosaic (not drawn). The picture's
 * registers are the frame's at its VBlank: the game changes the display
 * only in VBlank (its one mid-frame IRQ is the music's). */

#include <string.h>

#include "gba/gba.h"
#include "host_platform.h"
#include "ppu_hd.h"

#define WIDTH  HOST_SCREEN_WIDTH
#define HEIGHT HOST_SCREEN_HEIGHT
/* The bitmap modes' frames are the GBA's 240x160, whatever the screen. */
#define BITMAP_W 240
#define BITMAP_H 160
#ifdef HOST_WIDE_BG
/* Widescreen (gba/host.h's HOST_WIDE_BG): what isn't a level layer is
 * 240-pixel art (the menus, the cutscenes, the warp room), centred as
 * screen.h's DISPLAY_ART_X centres it: outside it, the backdrop. */
#define ART_X0 ((WIDTH - BITMAP_W) / 2)
#endif

#define DISPCNT_FRAME_SELECT 0x0010
#define DISPCNT_HBLANK_FREE  0x0020

#define TRANSPARENT 0x8000
#define OBJ_SEMI      1
#define OBJ_UNWRITTEN 0x80

enum {
    LAYER_BG0,
    LAYER_BG1,
    LAYER_BG2,
    LAYER_BG3,
    LAYER_OBJ,
    LAYER_BD
};

struct ObjPixel {
    u16 color;
    u8 priority;
    u8 flags;
};

static const u8 sObjSize[4][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
    { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
};

static inline u16 Io16(const struct HdFrame *f, u32 offset)
{
    return *(const u16 *)(f->io + offset);
}

static inline u32 Io32(const struct HdFrame *f, u32 offset)
{
    return *(const u32 *)(f->io + offset);
}

static inline u16 Pltt(const struct HdFrame *f, u32 index)
{
    return *(const u16 *)(f->pltt + 2 * index) & 0x7FFF;
}

static inline u32 Rgb8(u32 c)
{
    u32 r = c & 0x1F;
    u32 g = (c >> 5) & 0x1F;
    u32 b = (c >> 10) & 0x1F;

    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return r << 16 | g << 8 | b;
}

/* ppu.c's colour effects with the coefficients in 256ths. */
static inline u32 Blend(u32 a, u32 eva, u32 b, u32 evb)
{
    u32 out = 0;
    s32 shift;

    for (shift = 0; shift <= 16; shift += 8) {
        u32 c = (((a >> shift) & 0xFF) * eva + ((b >> shift) & 0xFF) * evb) / 256;

        out |= (c > 0xFF ? 0xFF : c) << shift;
    }
    return out;
}

static inline u32 Brighten(u32 a, u32 evy)
{
    u32 out = 0;
    s32 shift;

    for (shift = 0; shift <= 16; shift += 8) {
        u32 c = (a >> shift) & 0xFF;

        out |= (c + (0xFF - c) * evy / 256) << shift;
    }
    return out;
}

static inline u32 Darken(u32 a, u32 evy)
{
    u32 r = (a >> 16) & 0xFF;
    u32 g = (a >> 8) & 0xFF;
    u32 b = a & 0xFF;

    r -= r * evy / 256;
    g -= (g * evy + 255) / 256;
    b -= (b * evy + 255) / 256;
    return r << 16 | g << 8 | b;
}

static inline s32 RefPoint(u32 reg)
{
    return (s32)(reg << 4) >> 4;
}

/* Floor division by a positive number. */
static inline s32 FloorDiv(s32 a, s32 b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* A text BG's texel at (tx, ty) of its map (already wrapped). */
static inline u16 TextTexel(const struct HdFrame *f, s32 bg, u16 cnt, u32 tx, u32 ty)
{
    u32 charBase = ((cnt >> 2) & 3) * 0x4000;
    u32 screenBase = ((cnt >> 8) & 0x1F) * 0x800;
    u32 size = cnt >> 14;
    u32 block = 0;
    u32 entry;
    u32 tile;
    u32 px;
    u32 py;
    u32 addr;
    u32 index;

#ifdef HOST_WIDE_BG
    if (cnt & HOST_BGCNT_WIDE_MAP) {
        /* a level layer's 64x32 map past the GBA's VRAM (gba/host.h) */
        entry = *(const u16 *)(f->vram + HOST_WIDE_BG_MAPS + bg * HOST_WIDE_BG_MAP +
                               ((ty & 0xFF) >> 3) * 128 + ((tx & 0x1FF) >> 3) * 2);
        goto have_entry;
    }
#endif
    if (tx >= 256) {
        block = 1;
    }
    if (ty >= 256) {
        block += size == 3 ? 2 : 1;
    }
    entry = *(const u16 *)(f->vram + ((screenBase + block * 0x800 + ((ty & 0xFF) >> 3) * 64 +
                                       ((tx & 0xFF) >> 3) * 2) &
                                      0xFFFF));
#ifdef HOST_WIDE_BG
have_entry:
#endif
    tile = entry & 0x3FF;
    px = tx & 7;
    py = ty & 7;
    if (entry & 0x400) {
        px = 7 - px;
    }
    if (entry & 0x800) {
        py = 7 - py;
    }
    if (cnt & BGCNT_256COLOR) {
        addr = charBase + tile * 64 + py * 8 + px;
        index = addr < 0x10000 ? f->vram[addr] : 0;
        return index ? Pltt(f, index) : TRANSPARENT;
    }
    addr = charBase + tile * 32 + py * 4 + px / 2;
    index = addr < 0x10000 ? (f->vram[addr] >> ((px & 1) * 4)) & 0xF : 0;
    return index ? Pltt(f, (entry >> 12) * 16 + index) : TRANSPARENT;
}

#ifdef HOST_WIDE_BG
/* Is screen x past level layer `bg`'s map (ppu.c's OutsideMap)? */
static int OutsideMapHd(const struct HdFrame *f, s32 bg, s32 x)
{
    const s16 *edges = (const s16 *)(f->vram + HOST_WIDE_BG_EDGES) + 2 * bg;

    return x < edges[0] || x >= edges[1];
}

/* ppu.c's Bar, at the view's scale. */
static int BarHd(const struct HdFrame *f, u32 bgOn, u32 mode, s32 X, s32 S)
{
    s32 x = X / S;
    int any = 0;
    int i;

    for (i = 0; i < 4; i++) {
        /* text BGs only: an affine BG's bit 13 is its wraparound */
        int text = mode == 0 || (mode == 1 && i < 2);

        if (text && (bgOn & (1 << i)) &&
            (Io16(f, REG_OFFSET_BG0CNT + 2 * i) & HOST_BGCNT_WIDE_MAP)) {
            if (!OutsideMapHd(f, i, x)) {
                return 0;
            }
            any = 1;
        }
    }
    return any || x < ART_X0 || x >= ART_X0 + BITMAP_W;
}

/* Outside the centred 240-pixel art (ART_X0), at the view's scale. */
static void ClipToArt(u16 *out, s32 outW, s32 S)
{
    s32 x;

    for (x = 0; x < outW; x++) {
        if (x < ART_X0 * S || x >= (ART_X0 + BITMAP_W) * S) {
            out[x] = TRANSPARENT;
        }
    }
}
#endif

static void TextBgLine(const struct HdView *v, s32 bg, s32 Y, u16 *out, s32 outW)
{
    const struct HdFrame *f = v->frame;
    s32 S = v->scale;
    u16 cnt = Io16(f, REG_OFFSET_BG0CNT + 2 * bg);
    u32 widthMask = (cnt >> 14) & 1 ? 0x1FF : 0xFF;
    u32 heightMask = (cnt >> 14) & 2 ? 0x1FF : 0xFF;
#ifdef HOST_WIDE_BG
    if (cnt & HOST_BGCNT_WIDE_MAP) {
        widthMask = 0x1FF;
        heightMask = 0xFF;
    }
#endif
    s32 hofs = (s32)(Io16(f, REG_OFFSET_BG0HOFS + 4 * bg) & 0x1FF) * S + v->bgX[bg];
#ifdef HOST_WIDE_BG
    /* not a level layer: 240-pixel art, in the centred window (ppu.c) */
    if (!(cnt & HOST_BGCNT_WIDE_MAP)) {
        hofs -= ART_X0 * S;
    }
#endif
    s32 vofs = (s32)(Io16(f, REG_OFFSET_BG0VOFS + 4 * bg) & 0x1FF) * S + v->bgY[bg];
    u32 ty = (u32)FloorDiv(Y + vofs, S) & heightMask;
    s32 col = FloorDiv(hofs, S);
    s32 X = -(hofs - col * S); /* where column `col` starts */

    for (; X < outW; col++, X += S) {
        u16 c = TextTexel(f, bg, cnt, (u32)col & widthMask, ty);
        s32 x0 = X < 0 ? 0 : X;
        s32 x1 = X + S > outW ? outW : X + S;
        s32 x;

#ifdef HOST_WIDE_BG
        if ((cnt & HOST_BGCNT_WIDE_MAP) && OutsideMapHd(f, bg, (X + S / 2) / S)) {
            c = TRANSPARENT;
        }
#endif

        for (x = x0; x < x1; x++) {
            out[x] = c;
        }
    }
#ifdef HOST_WIDE_BG
    if (!(cnt & HOST_BGCNT_WIDE_MAP)) {
        ClipToArt(out, outW, S);
    }
#endif
}

/* An affine or bitmap BG: each hi-res pixel takes the GBA pixel's texel
 * (screen x, y), moved by bgX/bgY (identity matrices only). */
static void AffineBgLine(const struct HdView *v, s32 bg, s32 Y, u32 mode, u32 dispcnt, u16 *out,
                         s32 outW)
{
    const struct HdFrame *f = v->frame;
    s32 S = v->scale;
    s32 i = bg - 2;
    u16 cnt = Io16(f, REG_OFFSET_BG0CNT + 2 * bg);
    s32 pa = (s16)Io16(f, REG_OFFSET_BG2PA + 0x10 * i);
    s32 pb = (s16)Io16(f, REG_OFFSET_BG2PB + 0x10 * i);
    s32 pc = (s16)Io16(f, REG_OFFSET_BG2PC + 0x10 * i);
    s32 pd = (s16)Io16(f, REG_OFFSET_BG2PD + 0x10 * i);
    s32 ys = FloorDiv(Y + v->bgY[bg], S);
    s32 refX = RefPoint(Io32(f, REG_OFFSET_BG2X + 0x10 * i)) + ys * pb;
    s32 refY = RefPoint(Io32(f, REG_OFFSET_BG2Y + 0x10 * i)) + ys * pd;
    /* (widescreen: the game's reference point already centres the art,
     * DISPLAY_ART_X; ClipToArt below cuts it to the window) */
    s32 bgX = v->bgX[bg];
    s32 xs = FloorDiv(bgX, S);
    s32 X = -(bgX - xs * S);

    for (; X < outW; xs++, X += S) {
        s32 tx = (refX + xs * pa) >> 8;
        s32 ty = (refY + xs * pc) >> 8;
        u16 color = TRANSPARENT;
        s32 x0 = X < 0 ? 0 : X;
        s32 x1 = X + S > outW ? outW : X + S;
        s32 x;

        if (mode <= 2) {
            u32 size = 128 << (cnt >> 14);
            u32 charBase = ((cnt >> 2) & 3) * 0x4000;
            u32 screenBase = ((cnt >> 8) & 0x1F) * 0x800;

            if (cnt & BGCNT_WRAP) {
                tx &= size - 1;
                ty &= size - 1;
            }
            if (tx >= 0 && ty >= 0 && tx < (s32)size && ty < (s32)size) {
                u32 tile = f->vram[(screenBase + (ty >> 3) * (size >> 3) + (tx >> 3)) & 0xFFFF];
                u32 addr = charBase + tile * 64 + (ty & 7) * 8 + (tx & 7);
                u32 index = addr < 0x10000 ? f->vram[addr] : 0;

                if (index) {
                    color = Pltt(f, index);
                }
            }
        } else if (mode == 3) {
            if (tx >= 0 && ty >= 0 && tx < BITMAP_W && ty < BITMAP_H) {
                color = *(const u16 *)(f->vram + (ty * BITMAP_W + tx) * 2) & 0x7FFF;
            }
        } else if (mode == 4) {
            u32 base = (dispcnt & DISPCNT_FRAME_SELECT) ? 0xA000 : 0;

            if (tx >= 0 && ty >= 0 && tx < BITMAP_W && ty < BITMAP_H) {
                u32 index = f->vram[base + ty * BITMAP_W + tx];

                if (index) {
                    color = Pltt(f, index);
                }
            }
        } else if (mode == 5) {
            u32 base = (dispcnt & DISPCNT_FRAME_SELECT) ? 0xA000 : 0;

            if (tx >= 0 && ty >= 0 && tx < 160 && ty < 128) {
                color = *(const u16 *)(f->vram + base + (ty * 160 + tx) * 2) & 0x7FFF;
            }
        }
        for (x = x0; x < x1; x++) {
            out[x] = color;
        }
    }
#ifdef HOST_WIDE_BG
    ClipToArt(out, outW, S);
#endif
}

static inline u32 ObjTexel(const struct HdFrame *f, u32 attr0, u32 attr2, u32 width, u32 lx, u32 ly,
                           u32 map1D)
{
    u32 tile = attr2 & 0x3FF;
    u32 addr;

    if (attr0 & (1 << 13)) {
        if (map1D) {
            addr = tile * 32 + (ly >> 3) * width * 8 + (lx >> 3) * 64 + (ly & 7) * 8 + (lx & 7);
        } else {
            tile &= ~1u;
            addr = ((tile * 32) & 0x7C00) + (ly >> 3) * 0x400 + (ly & 7) * 8 +
                   (((tile * 32) + (lx >> 3) * 64 + (lx & 7)) & 0x3FF);
        }
        return f->vram[0x10000 + (addr & 0x7FFF)];
    }
    if (map1D) {
        addr = tile * 32 + (ly >> 3) * width * 4 + (lx >> 3) * 32 + (ly & 7) * 4 + (lx & 7) / 2;
    } else {
        addr = ((tile * 32) & 0x7C00) + (ly >> 3) * 0x400 + (ly & 7) * 4 +
               (((tile * 32) + (lx >> 3) * 32 + (lx & 7) / 2) & 0x3FF);
    }
    return (f->vram[0x10000 + (addr & 0x7FFF)] >> ((lx & 1) * 4)) & 0xF;
}

static inline void PutObjPixel(const struct HdFrame *f, struct ObjPixel *line, u8 *window, s32 x,
                               u32 index, u32 attr0, u32 attr2, u32 priority, u32 flags)
{
    struct ObjPixel *p = &line[x];

    if (((attr0 >> 10) & 3) == 2) {
        if (index) {
            window[x] = 1;
        }
        return;
    }
    if (priority >= p->priority) {
        return;
    }
    if (index) {
        p->color = (attr0 & (1 << 13)) ? Pltt(f, 0x100 + index)
                                       : Pltt(f, 0x100 + (attr2 >> 12) * 16 + index);
        p->priority = priority;
        p->flags = flags;
    } else if (!(p->flags & OBJ_UNWRITTEN)) {
        p->priority = priority;
        p->flags = flags;
    }
}

/* A button prompt's pixel (prompts.c): its own picture and colours. */
static inline void PutPromptPixel(struct ObjPixel *line, s32 x, int prompt, u32 lx, u32 ly,
                                  u32 attr0, u32 priority, u32 flags)
{
    u32 index = lx < 16 && ly < 16 ? gHostPrompts.art[prompt][ly][lx] : 0;
    struct ObjPixel *p = &line[x];

    if (((attr0 >> 10) & 3) == 2 || priority >= p->priority) {
        return;
    }
    if (index) {
        p->color = gHostPrompts.pal[prompt][index] & 0x7FFF;
        p->priority = priority;
        p->flags = flags;
    } else if (!(p->flags & OBJ_UNWRITTEN)) {
        p->priority = priority;
        p->flags = flags;
    }
}

static void ObjLine(const struct HdView *v, s32 Y, u32 dispcnt, struct ObjPixel *line, u8 *window,
                    s32 outW)
{
    const struct HdFrame *f = v->frame;
    s32 S = v->scale;
    u32 map1D = (dispcnt & DISPCNT_OBJ_1D_MAP) != 0;
    u32 mode = dispcnt & 7;
    s32 cycles = (dispcnt & DISPCNT_HBLANK_FREE) ? 954 : 1210;
    const u16 *oam = (const u16 *)f->oam;
    s32 i;

    for (i = 0; i < 128; i++) {
        u32 attr0 = oam[4 * i];
        u32 attr1 = oam[4 * i + 1];
        u32 attr2 = oam[4 * i + 2];
        u32 affine = (attr0 >> 8) & 1;
        u32 shape = attr0 >> 14;
        u32 sizeIndex = attr1 >> 14;
        u32 width = sObjSize[shape][sizeIndex][0];
        u32 height = sObjSize[shape][sizeIndex][1];
        u32 boxW = width;
        u32 boxH = height;
        s32 objY = attr0 & 0xFF;
        s32 objX = (s32)((attr1 & 0x1FF) << 23) >> 23;
        u32 priority = (attr2 >> 10) & 3;
        u32 flags = ((attr0 >> 10) & 3) == 1 ? OBJ_SEMI : 0;
        s32 X0;
        s32 Y0;
        s32 inY;
        s32 xa;
        s32 xb;
        s32 X;

        if (!affine && (attr0 & (1 << 9))) {
            continue;
        }
        if (affine) {
            boxW <<= (attr0 >> 9) & 1;
            boxH <<= (attr0 >> 9) & 1;
        }
        if ((u32)objY >= HEIGHT && (u32)objY + boxH < 228) {
            continue;
        }
        if ((u32)(attr1 & 0x1FF) >= WIDTH && (attr1 & 0x1FF) + boxW < 512) {
            continue;
        }
        /* An OBJ near the bottom of the 256-line range comes in at the
         * top (ppu.c's `(y - objY) & 0xFF`). */
        if (objY >= HEIGHT && objY + (s32)boxH > 256) {
            objY -= 256;
        }
        X0 = objX * S + v->objX[i];
        Y0 = objY * S + v->objY[i];
        inY = FloorDiv(Y - Y0, S);
        if (inY < 0 || inY >= (s32)boxH) {
            continue;
        }
        if (mode >= 3 && (attr2 & 0x3FF) < 512) {
            goto spent;
        }
        xa = X0 < 0 ? 0 : X0;
        xb = X0 + (s32)boxW * S;
        if (xb > outW) {
            xb = outW;
        }
        if (affine) {
            u32 m = ((attr1 >> 9) & 0x1F) * 16;
            s32 pa = (s16)oam[m + 3];
            s32 pb = (s16)oam[m + 7];
            s32 pc = (s16)oam[m + 11];
            s32 pd = (s16)oam[m + 15];
            s32 dy = inY - (s32)(boxH / 2);

            for (X = xa; X < xb; X++) {
                s32 dx = FloorDiv(X - X0, S) - (s32)(boxW / 2);
                s32 lx = ((pa * dx + pb * dy) >> 8) + (s32)(width / 2);
                s32 ly = ((pc * dx + pd * dy) >> 8) + (s32)(height / 2);

                if (lx < 0 || ly < 0 || lx >= (s32)width || ly >= (s32)height) {
                    continue;
                }
                PutObjPixel(f, line, window, X, ObjTexel(f, attr0, attr2, width, lx, ly, map1D),
                            attr0, attr2, priority, flags);
            }
        } else {
            u32 ly = (attr1 & (1 << 13)) ? height - 1 - inY : (u32)inY;
            int prompt = HostPromptGlyph(attr0, attr2);

            for (X = xa; X < xb; X++) {
                s32 lx = FloorDiv(X - X0, S);

                if (attr1 & (1 << 12)) {
                    lx = width - 1 - lx;
                }
                if (prompt >= 0) {
                    PutPromptPixel(line, X, prompt, (u32)lx, ly, attr0, priority, flags);
                    continue;
                }
                PutObjPixel(f, line, window, X, ObjTexel(f, attr0, attr2, width, lx, ly, map1D),
                            attr0, attr2, priority, flags);
            }
        }
    spent:
        cycles -= affine ? 10 + 2 * boxW : boxW;
        if (cycles <= 0) {
            break;
        }
    }
}

static inline s32 InSpan(u32 v, u32 reg, u32 limit)
{
    u32 a = reg >> 8;
    u32 b = reg & 0xFF;

    if (b > limit || (a > b && b == 0)) {
        b = limit;
    }
    if (a <= b) {
        return v >= a && v < b;
    }
    return v >= a || v < b;
}

void HostPpuHdLine(const struct HdView *v, s32 Y, u32 *row)
{
    const struct HdFrame *f = v->frame;
    s32 S = v->scale;
    s32 outW = WIDTH * S;
    u32 dispcnt = Io16(f, REG_OFFSET_DISPCNT);
    u32 mode = dispcnt & 7;
    u32 bldcnt = Io16(f, REG_OFFSET_BLDCNT);
    u32 effect = (bldcnt >> 6) & 3;
    u32 eva = v->eva;
    u32 evb = v->evb;
    u32 evy = v->evy;
    u32 windows = dispcnt & (DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJWIN_ON);
    u32 bgOn;
    u32 bgPrio[4];
    u32 target2Any;
    u32 backdrop;
    static __thread u16 bgLine[4][HOST_HD_MAX_WIDTH];
    struct ObjPixel objLine[HOST_HD_MAX_WIDTH];
    u8 objWindow[HOST_HD_MAX_WIDTH];
    s32 X;
    s32 i;

    if (dispcnt & DISPCNT_FORCED_BLANK) {
        for (X = 0; X < outW; X++) {
            row[X] = 0xFFFFFF;
        }
        return;
    }
    switch (mode) {
    case 0:
        bgOn = 0xF;
        break;
    case 1:
        bgOn = 0x7;
        break;
    case 2:
        bgOn = 0xC;
        break;
    default:
        bgOn = 0x4;
        break;
    }
    bgOn &= dispcnt >> 8;
    for (i = 0; i < 4; i++) {
        bgPrio[i] = Io16(f, REG_OFFSET_BG0CNT + 2 * i) & 3;
        if (!(bgOn & (1 << i))) {
            for (X = 0; X < outW; X++) {
                bgLine[i][X] = TRANSPARENT;
            }
            continue;
        }
        if (mode == 0 || (mode == 1 && i < 2)) {
            TextBgLine(v, i, Y, bgLine[i], outW);
        } else {
            AffineBgLine(v, i, Y, mode, dispcnt, bgLine[i], outW);
        }
    }
    for (X = 0; X < outW; X++) {
        objLine[X].color = TRANSPARENT;
        objLine[X].priority = 4;
        objLine[X].flags = OBJ_UNWRITTEN;
        objWindow[X] = 0;
    }
    if (dispcnt & DISPCNT_OBJ_ON) {
        ObjLine(v, Y, dispcnt, objLine, objWindow, outW);
    }
    target2Any = (bldcnt >> 13) & 1;
    for (i = 0; i < 4; i++) {
        if ((bldcnt & (0x100 << i)) && (dispcnt & (DISPCNT_BG0_ON << i))) {
            target2Any = 1;
        }
    }
    backdrop = Rgb8(Pltt(f, 0));
    for (X = 0; X < outW; X++) {
        u32 control = 0x3F;

        /* Most hi-res pixels have the same layers as the one before
         * (every pixel is `scale` wide): the same picture. */
        if (X > 0 && !windows && bgLine[0][X] == bgLine[0][X - 1] &&
            bgLine[1][X] == bgLine[1][X - 1] && bgLine[2][X] == bgLine[2][X - 1] &&
            bgLine[3][X] == bgLine[3][X - 1] &&
            *(const u32 *)&objLine[X] == *(const u32 *)&objLine[X - 1]) {
            row[X] = row[X - 1];
            continue;
        }
        u32 layers[2] = { LAYER_BD, LAYER_BD };
        u32 colors[2] = { 0, 0 };
        u32 found = 0;
        u32 p;
        u32 top;
        u32 out;
        s32 semi = 0;

        if (windows) {
            u32 winin = Io16(f, REG_OFFSET_WININ);
            u32 winout = Io16(f, REG_OFFSET_WINOUT);
            u32 x = (u32)(X / S);
            u32 y = (u32)(Y / S);

            if ((dispcnt & DISPCNT_WIN0_ON) && InSpan(x, Io16(f, REG_OFFSET_WIN0H), WIDTH) &&
                InSpan(y, Io16(f, REG_OFFSET_WIN0V), HEIGHT)) {
                control = winin & 0x3F;
            } else if ((dispcnt & DISPCNT_WIN1_ON) && InSpan(x, Io16(f, REG_OFFSET_WIN1H), WIDTH) &&
                       InSpan(y, Io16(f, REG_OFFSET_WIN1V), HEIGHT)) {
                control = (winin >> 8) & 0x3F;
            } else if ((dispcnt & DISPCNT_OBJWIN_ON) && objWindow[X]) {
                control = (winout >> 8) & 0x3F;
            } else {
                control = winout & 0x3F;
            }
        }
        for (p = 0; p < 4 && found < 2; p++) {
            if ((control & 0x10) && objLine[X].color != TRANSPARENT && objLine[X].priority == p) {
                colors[found] = objLine[X].color;
                if (found == 0) {
                    semi = objLine[X].flags & OBJ_SEMI;
                }
                layers[found++] = LAYER_OBJ;
            }
            for (i = 0; i < 4 && found < 2; i++) {
                if ((bgOn & control & (1 << i)) && bgPrio[i] == p && bgLine[i][X] != TRANSPARENT) {
                    colors[found] = bgLine[i][X];
                    layers[found++] = i;
                }
            }
        }
        top = layers[0];
        out = top == LAYER_BD ? backdrop : Rgb8(colors[0]);

        if (semi && (bldcnt & (0x100 << layers[1]))) {
            u32 under = layers[1] == LAYER_BD ? backdrop : Rgb8(colors[1]);

            out = Blend(out, eva, under, evb);
        } else if (!(control & 0x20)) {
        } else if (semi && target2Any) {
            if (effect == 2) {
                out = Brighten(out, evy);
            } else if (effect == 3) {
                out = Darken(out, evy);
            }
        } else if (bldcnt & (1 << top)) {
            if (effect == 1 && top != LAYER_BD && (bldcnt & (0x100 << layers[1]))) {
                u32 under = layers[1] == LAYER_BD ? backdrop : Rgb8(colors[1]);

                out = Blend(out, eva, under, evb);
            } else if (effect == 2) {
                out = Brighten(out, evy);
            } else if (effect == 3) {
                out = Darken(out, evy);
            }
        }
#ifdef HOST_WIDE_BG
        /* the bars beside the centred art: black, not the backdrop (ppu.c) */
        if (top == LAYER_BD && BarHd(f, bgOn, mode, X, S)) {
            out = 0;
        }
#endif
        row[X] = out;
    }
}

void HostPpuHdStill(struct HdView *v, const struct HdFrame *f, s32 scale)
{
    u32 bldalpha = Io16(f, REG_OFFSET_BLDALPHA);
    u32 eva = bldalpha & 0x1F;
    u32 evb = (bldalpha >> 8) & 0x1F;
    u32 evy = Io16(f, REG_OFFSET_BLDY) & 0x1F;

    memset(v, 0, sizeof(*v));
    v->frame = f;
    v->scale = scale;
    v->eva = (eva > 16 ? 16 : eva) * 16;
    v->evb = (evb > 16 ? 16 : evb) * 16;
    v->evy = (evy > 16 ? 16 : evy) * 16;
}

/* What hi-res line Y is drawn from: each BG's source row, each OBJ's row
 * (or none), and the GBA line (the windows). Two lines with the same key
 * are the same picture. */
#define KEY_LEN (4 + 128 + 1)

static void LineKey(const struct HdView *v, s32 Y, s32 *key)
{
    const struct HdFrame *f = v->frame;
    s32 S = v->scale;
    const u16 *oam = (const u16 *)f->oam;
    s32 i;

    for (i = 0; i < 4; i++) {
        key[i] =
            FloorDiv(Y + v->bgY[i] + (s32)(Io16(f, REG_OFFSET_BG0VOFS + 4 * i) & 0x1FF) * S, S);
    }
    for (i = 0; i < 128; i++) {
        u32 attr0 = oam[4 * i];
        u32 attr1 = oam[4 * i + 1];
        u32 boxH = sObjSize[attr0 >> 14][attr1 >> 14][1];
        s32 objY = attr0 & 0xFF;
        s32 inY;

        if (!(attr0 & 0x100) && (attr0 & 0x200)) {
            key[4 + i] = -1;
            continue;
        }
        if (attr0 & 0x100) {
            boxH <<= (attr0 >> 9) & 1;
        }
        if (objY >= HEIGHT && objY + (s32)boxH > 256) {
            objY -= 256;
        }
        inY = FloorDiv(Y - (objY * S + v->objY[i]), S);
        key[4 + i] = (inY < 0 || inY >= (s32)boxH) ? -1 : inY;
    }
    key[4 + 128] = Y / S;
}

void HostPpuHdLines(const struct HdView *v, s32 first, s32 end, u32 *out, s32 stride)
{
    s32 keys[2][KEY_LEN];
    s32 Y;
    int have = 0;

    for (Y = first; Y < end; Y++) {
        s32 *key = keys[Y & 1];

        LineKey(v, Y, key);
        if (have && memcmp(key, keys[(Y - 1) & 1], sizeof(keys[0])) == 0) {
            memcpy(out + (size_t)Y * stride, out + (size_t)(Y - 1) * stride,
                   (size_t)WIDTH * v->scale * 4);
        } else {
            HostPpuHdLine(v, Y, out + (size_t)Y * stride);
        }
        have = 1;
    }
}
