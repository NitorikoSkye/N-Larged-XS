#ifndef GUARD_PC_PPU_HD_H
#define GUARD_PC_PPU_HD_H

/* ppu_hd.c: the PPU at `scale` times the GBA's resolution, from a kept
 * frame, with its layers moved by fractions of a pixel (host_interp.h). */

#include "gba/types.h"
#include "gba/host.h"

#define HOST_HD_MAX_SCALE 4
#define HOST_HD_MAX_WIDTH (HOST_SCREEN_WIDTH * HOST_HD_MAX_SCALE)

/* A frame's display state, as the PPU drew it. */
struct HdFrame {
    u8 io[0x60];
    u8 vram[HOST_VRAM_SIZE];
    u8 pltt[HOST_PLTT_SIZE];
    u8 oam[HOST_OAM_SIZE];
};

/* A frame and how far its layers move: the BGs' scrolls and the OBJs'
 * positions, in 1/scale pixels; the colour effects' coefficients in
 * 256ths. */
struct HdView {
    const struct HdFrame *frame;
    s32 scale;
    s32 bgX[4];
    s32 bgY[4];
    s32 objX[128];
    s32 objY[128];
    u32 eva;
    u32 evb;
    u32 evy;
};

/* The view of `f` with nothing moved. */
void HostPpuHdStill(struct HdView *v, const struct HdFrame *f, s32 scale);
/* Hi-res line Y (0 to 160 * scale - 1) into `row` (240 * scale pixels). */
void HostPpuHdLine(const struct HdView *v, s32 Y, u32 *row);
/* Lines first to end - 1 into `out` (`stride` pixels a line): a line drawn
 * from the same rows of every layer as the one before is copied. */
void HostPpuHdLines(const struct HdView *v, s32 first, s32 end, u32 *out, s32 stride);

#endif /* GUARD_PC_PPU_HD_H */
