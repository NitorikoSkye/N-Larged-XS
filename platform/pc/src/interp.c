/* Frame interpolation (host_interp.h; docs/platforms.md, "Frame
 * interpolation").
 *
 * Each game frame's display state is kept at its VBlank (HostInterpCapture):
 * the registers, VRAM, palette RAM, OAM, and for each OAM entry the game
 * object that drew it, the how-manyth time this frame it drew, and where
 * it stood on the screen (interp_tags.cpp). The last two frames are
 * matched once:
 *
 * - a text BG moves by its scroll's change (an affine BG only with the
 *   identity matrix in both, by its reference point's), when the BG is set
 *   up the same in both and the change is under 48 pixels (more is a jump:
 *   a new room, a wrapped map);
 * - an OAM entry moves by its object's change of place (the same object
 *   drawn the same how-manyth time in the other frame), so that an object
 *   whose animation changed its pieces still moves as a whole; an entry
 *   no object owns (the HUD's text, the menus' logos) by its own change
 *   of place, if the other frame has an entry in the same slot with the
 *   same tile, size and palette, under 32 pixels away;
 * - the colour effects' coefficients (a fade) step between the frames'
 *   when BLDCNT is the same in both.
 *
 * A picture `alpha` of the way from the earlier frame to the later is the
 * nearer frame (the earlier below one half), with each of those moved to
 * where it is at alpha, drawn by ppu_hd.c. Anything else (a new object,
 * tiles that changed) is the nearer frame's as it is. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gba/gba.h"
#include "host_interp_tags.h"
#include "host_platform.h"
#include "ppu_hd.h"

struct Owner {
    const void *obj;
    s32 x;
    s32 y;
    u16 occurrence;
    u8 valid;
};

struct Kept {
    struct HdFrame f;
    struct Owner owner[128];
    /* Matched with the other frame: how each entry moves from the earlier
     * frame to the later, in pixels. */
    s32 dx[128];
    s32 dy[128];
    u8 moves[128];
};

static struct Kept *sKept[2];
static int sNewest = -1;
static int sCount;

/* The BGs' moves from the earlier frame to the later, in 1/256 pixels. */
static s32 sBgDX[4];
static s32 sBgDY[4];
static u8 sBgMoves[4];
static int sEffectsMove;

/* --- The tags (interp_tags.cpp) -------------------------------------------- */

static struct Owner sTag;
static int sTagDepth;
static struct Owner sSlot[128];
static struct Owner sCommitted[128];
static struct Owner sQueue[256];
static int sQueueCount;
static int sQueueFlushing;

#define OCC_SIZE 1024
static const void *sOccObj[OCC_SIZE];
static u16 sOccCount[OCC_SIZE];
static u32 sOccGen[OCC_SIZE];
static u32 sGen = 1;

void HostInterpTagObject(const void *object, s32 x, s32 y)
{
    u32 h;

    if (sTagDepth++ > 0) {
        return; /* the outermost entry point's place */
    }
    h = (u32)(((uintptr_t)object >> 3) * 2654435761u) & (OCC_SIZE - 1);
    while (sOccGen[h] == sGen && sOccObj[h] != object) {
        h = (h + 1) & (OCC_SIZE - 1);
    }
    if (sOccGen[h] != sGen) {
        sOccGen[h] = sGen;
        sOccObj[h] = object;
        sOccCount[h] = 0;
    }
    sTag.obj = object;
    sTag.x = x;
    sTag.y = y;
    sTag.occurrence = sOccCount[h]++;
    sTag.valid = 1;
}

void HostInterpTagNone(void)
{
    if (sTagDepth > 0 && --sTagDepth == 0) {
        sTag.valid = 0;
    }
}

void HostInterpNoteSlot(s32 slot)
{
    if (slot >= 0 && slot < 128) {
        sSlot[slot] = sTag;
        if (sTagDepth == 0) {
            sSlot[slot].valid = 0;
        }
    }
}

void HostInterpNoteQueued(void)
{
    if (sQueueCount < 256) {
        sQueue[sQueueCount] = sTag;
        if (sTagDepth == 0) {
            sQueue[sQueueCount].valid = 0;
        }
        sQueueCount++;
    }
}

void HostInterpQueueFlushing(int on)
{
    sQueueFlushing = on;
    if (!on) {
        sQueueCount = 0;
    }
}

void HostInterpNoteAppend(s32 first, s32 n)
{
    s32 i;

    for (i = 0; i < n && first + i < 128; i++) {
        if (first + i < 0) {
            continue;
        }
        if (sQueueFlushing && i < sQueueCount) {
            sSlot[first + i] = sQueue[i];
        } else {
            sSlot[first + i].valid = 0;
        }
    }
}

void HostInterpNoteCommit(void)
{
    memcpy(sCommitted, sSlot, sizeof(sCommitted));
    sGen++;
}

/* --- Keeping and matching -------------------------------------------------- */

static void KeepInto(struct Kept *k)
{
    memcpy(k->f.io, gHostIo, sizeof(k->f.io));
    memcpy(k->f.vram, gHostVram, sizeof(k->f.vram));
    memcpy(k->f.pltt, gHostPltt, sizeof(k->f.pltt));
    memcpy(k->f.oam, gHostOam, sizeof(k->f.oam));
    memcpy(k->owner, sCommitted, sizeof(k->owner));
}

static inline u16 Io16(const struct HdFrame *f, u32 offset)
{
    return *(const u16 *)(f->io + offset);
}

static inline u32 Io32(const struct HdFrame *f, u32 offset)
{
    return *(const u32 *)(f->io + offset);
}

static inline s32 Wrap9(s32 d)
{
    return ((d + 256) & 511) - 256;
}

static int Visible(const struct HdFrame *f, int i)
{
    u16 attr0 = ((const u16 *)f->oam)[4 * i];

    return (attr0 & 0x100) || !(attr0 & 0x200);
}

static void EntryPos(const struct HdFrame *f, int i, s32 *x, s32 *y)
{
    const u16 *oam = (const u16 *)f->oam;

    *x = (s32)((oam[4 * i + 1] & 0x1FF) << 23) >> 23;
    *y = oam[4 * i] & 0xFF;
    if (*y >= 160) {
        *y -= 256;
    }
}

/* How `from`'s entries move to their places in `to` (the earlier frame's
 * moves forward, the later's backward: both from earlier to later). */
static void MatchEntries(struct Kept *from, const struct Kept *to, int fromIsLater)
{
    const u16 *a = (const u16 *)from->f.oam;
    const u16 *b = (const u16 *)to->f.oam;
    int i;
    int j;

    for (i = 0; i < 128; i++) {
        const struct Owner *o = &from->owner[i];
        s32 dx = 0;
        s32 dy = 0;
        int found = 0;

        from->moves[i] = 0;
        if (!Visible(&from->f, i)) {
            continue;
        }
        if (o->valid) {
            for (j = 0; j < 128; j++) {
                const struct Owner *p = &to->owner[j];

                if (p->valid && p->obj == o->obj && p->occurrence == o->occurrence && Visible(&to->f, j)) {
                    dx = p->x - o->x;
                    dy = p->y - o->y;
                    found = 1;
                    break;
                }
            }
            if (found && (dx > 64 || dx < -64 || dy > 64 || dy < -64)) {
                found = 0;
            }
        } else if (!to->owner[i].valid && Visible(&to->f, i) && (a[4 * i] & 0xFF00) == (b[4 * i] & 0xFF00) &&
                   (a[4 * i + 1] & 0xFE00) == (b[4 * i + 1] & 0xFE00) && a[4 * i + 2] == b[4 * i + 2]) {
            s32 x0;
            s32 y0;
            s32 x1;
            s32 y1;

            EntryPos(&from->f, i, &x0, &y0);
            EntryPos(&to->f, i, &x1, &y1);
            dx = x1 - x0;
            dy = y1 - y0;
            found = dx <= 32 && dx >= -32 && dy <= 32 && dy >= -32;
        }
        if (found && (dx != 0 || dy != 0)) {
            from->moves[i] = 1;
            from->dx[i] = fromIsLater ? -dx : dx;
            from->dy[i] = fromIsLater ? -dy : dy;
        }
    }
}

static void Match(void)
{
    struct Kept *later = sKept[sNewest];
    struct Kept *earlier = sKept[sNewest ^ 1];
    const struct HdFrame *p = &earlier->f;
    const struct HdFrame *c = &later->f;
    u32 dispP = Io16(p, REG_OFFSET_DISPCNT);
    u32 dispC = Io16(c, REG_OFFSET_DISPCNT);
    u32 mode = dispC & 7;
    int same = (dispP & 7) == mode;
    int i;

    for (i = 0; i < 4; i++) {
        u16 cnt = Io16(c, REG_OFFSET_BG0CNT + 2 * i);
        s32 dx = 0;
        s32 dy = 0;
        int ok = same && cnt == Io16(p, REG_OFFSET_BG0CNT + 2 * i) && (dispP & dispC & (DISPCNT_BG0_ON << i));

        if (ok && (mode == 0 || (mode == 1 && i < 2))) {
            dx = Wrap9(Io16(c, REG_OFFSET_BG0HOFS + 4 * i) - Io16(p, REG_OFFSET_BG0HOFS + 4 * i)) * 256;
            dy = Wrap9(Io16(c, REG_OFFSET_BG0VOFS + 4 * i) - Io16(p, REG_OFFSET_BG0VOFS + 4 * i)) * 256;
        } else if (ok && i >= 2) {
            u32 base = 0x10 * (i - 2);
            int identity = 1;
            int k;

            for (k = 0; k < 4; k++) {
                u16 want = (k == 0 || k == 3) ? 0x100 : 0;

                if (Io16(c, REG_OFFSET_BG2PA + base + 2 * k) != want ||
                    Io16(p, REG_OFFSET_BG2PA + base + 2 * k) != want) {
                    identity = 0;
                }
            }
            if (identity) {
                dx = ((s32)(Io32(c, REG_OFFSET_BG2X + base) << 4) >> 4) -
                     ((s32)(Io32(p, REG_OFFSET_BG2X + base) << 4) >> 4);
                dy = ((s32)(Io32(c, REG_OFFSET_BG2Y + base) << 4) >> 4) -
                     ((s32)(Io32(p, REG_OFFSET_BG2Y + base) << 4) >> 4);
            } else {
                ok = 0;
            }
        }
        sBgMoves[i] = ok && (dx != 0 || dy != 0) && dx <= 48 * 256 && dx >= -48 * 256 && dy <= 48 * 256 &&
                      dy >= -48 * 256;
        sBgDX[i] = dx;
        sBgDY[i] = dy;
    }
    sEffectsMove = same && Io16(p, REG_OFFSET_BLDCNT) == Io16(c, REG_OFFSET_BLDCNT);
    MatchEntries(earlier, later, 0);
    MatchEntries(later, earlier, 1);
}

void HostInterpCapture(void)
{
    int k = sNewest < 0 ? 0 : sNewest ^ 1;

    if (sKept[k] == NULL) {
        sKept[k] = calloc(1, sizeof(struct Kept));
        if (sKept[k] == NULL) {
            return;
        }
    }
    KeepInto(sKept[k]);
    sNewest = k;
    if (sCount < 2) {
        sCount++;
    }
    if (sCount == 2) {
        Match();
    }
}

int HostInterpReady(void)
{
    return sCount == 2;
}

void HostInterpReset(void)
{
    sCount = 0;
    sNewest = -1;
}

/* --- Drawing ---------------------------------------------------------------- */

struct RenderJob {
    struct HdView view;
    u32 *out;
    s32 width;
};

static void RunLines(void *ctx, int first, int end)
{
    struct RenderJob *job = ctx;

    HostPpuHdLines(&job->view, first, end, job->out, job->width);
}

static s32 Round(double v)
{
    return (s32)floor(v + 0.5);
}

static void SerialParallel(HostLineRun run, void *ctx, int lines)
{
    run(ctx, 0, lines);
}

static void Effect(const struct HdFrame *p, const struct HdFrame *c, double alpha, u32 *eva, u32 *evb, u32 *evy)
{
    u32 ap = Io16(p, REG_OFFSET_BLDALPHA);
    u32 ac = Io16(c, REG_OFFSET_BLDALPHA);
    u32 vals[3][2] = {
        { ap & 0x1F, ac & 0x1F },
        { (ap >> 8) & 0x1F, (ac >> 8) & 0x1F },
        { Io16(p, REG_OFFSET_BLDY) & 0x1F, Io16(c, REG_OFFSET_BLDY) & 0x1F },
    };
    u32 *outs[3] = { eva, evb, evy };
    int i;

    for (i = 0; i < 3; i++) {
        double a = vals[i][0] > 16 ? 16 : vals[i][0];
        double b = vals[i][1] > 16 ? 16 : vals[i][1];

        *outs[i] = (u32)Round((a + (b - a) * alpha) * 16);
    }
}

void HostInterpRender(double alpha, int scale, u32 *out,
                      void (*parallel)(HostLineRun run, void *ctx, int lines))
{
    static struct RenderJob job;
    struct Kept *later = sKept[sNewest];
    struct Kept *earlier = sKept[sNewest ^ 1];
    struct Kept *base;
    double t;
    int i;

    if (alpha > 1) {
        alpha = 1;
    }
    if (alpha < 0) {
        alpha = 0;
    }
    base = alpha < 0.5 ? earlier : later;
    t = alpha < 0.5 ? alpha : alpha - 1.0;
    HostPpuHdStill(&job.view, &base->f, scale);
    for (i = 0; i < 4; i++) {
        if (sBgMoves[i]) {
            job.view.bgX[i] = Round(t * sBgDX[i] * scale / 256.0);
            job.view.bgY[i] = Round(t * sBgDY[i] * scale / 256.0);
        }
    }
    for (i = 0; i < 128; i++) {
        if (base->moves[i]) {
            job.view.objX[i] = Round(t * base->dx[i] * scale);
            job.view.objY[i] = Round(t * base->dy[i] * scale);
        }
    }
    if (sEffectsMove) {
        Effect(&earlier->f, &later->f, alpha, &job.view.eva, &job.view.evb, &job.view.evy);
    }
    job.out = out;
    job.width = HOST_SCREEN_WIDTH * scale;
    (parallel != NULL ? parallel : SerialParallel)(RunLines, &job, HOST_SCREEN_HEIGHT * scale);
}

/* --- --hd-check and --interp-dump (headless tests) -------------------------- */

int gHostHdCheck;
const char *gHostInterpDumpDir;
u32 gHostInterpDumpAt;

static u32 sCheckFrames;
static u32 sCheckBad;
static u64 sCheckPixels;

void HostHdCheckFrame(void)
{
    static struct Kept still;
    static u32 out[HOST_SCREEN_WIDTH * 2 * HOST_SCREEN_HEIGHT * 2];
    struct HdView v;
    s32 scale;

    if (gHostHdCheck) {
        KeepInto(&still);
        for (scale = 1; scale <= 2; scale++) {
            u32 bad = 0;
            s32 x;
            s32 y;

            HostPpuHdStill(&v, &still.f, scale);
            HostPpuHdLines(&v, 0, HOST_SCREEN_HEIGHT * scale, out, HOST_SCREEN_WIDTH * scale);
            for (y = 0; y < HOST_SCREEN_HEIGHT * scale; y++) {
                for (x = 0; x < HOST_SCREEN_WIDTH * scale; x++) {
                    if (out[y * HOST_SCREEN_WIDTH * scale + x] !=
                        (gHostScreen[(y / scale) * HOST_SCREEN_WIDTH + x / scale] & 0xFFFFFF)) {
                        bad++;
                    }
                }
            }
            if (bad != 0) {
                if (sCheckBad < 10) {
                    printf("hd-check: VBlank %u, scale %d: %u pixels differ\n", gHostVBlanks, scale, bad);
                }
                sCheckBad++;
                sCheckPixels += bad;
            }
        }
        sCheckFrames++;
    }
    if (getenv("HOST_HD_BENCH") != NULL && HostFrame() == gHostInterpDumpAt && HostInterpReady()) {
        static u32 big[HOST_SCREEN_WIDTH * 4 * HOST_SCREEN_HEIGHT * 4];
        clock_t t0 = clock();
        int k;

        for (k = 0; k < 50; k++) {
            HostInterpRender(k / 50.0, 4, big, NULL);
        }
        printf("hd-bench: %.2f ms a 4x picture (one thread)\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 50);
    }
    if (gHostInterpDumpDir != NULL) {
        u32 frame = HostFrame();

        HostInterpCapture();
        if (frame >= gHostInterpDumpAt && frame < gHostInterpDumpAt + 3 && HostInterpReady()) {
            static u32 big[HOST_SCREEN_WIDTH * 4 * HOST_SCREEN_HEIGHT * 4];
            int k;

            for (k = 0; k < 4; k++) {
                char path[512];

                HostInterpRender(k / 4.0, 4, big, NULL);
                snprintf(path, sizeof(path), "%s/%05u-%d.png", gHostInterpDumpDir, frame, k);
                HostWritePng(path, big, HOST_SCREEN_WIDTH * 4, HOST_SCREEN_HEIGHT * 4);
            }
        }
    }
}

void HostHdCheckEnd(void)
{
    if (gHostHdCheck) {
        printf("hd-check: %u frames at scales 1 and 2, %u differ (%llu pixels)\n", sCheckFrames, sCheckBad,
               (unsigned long long)sCheckPixels);
    }
}
