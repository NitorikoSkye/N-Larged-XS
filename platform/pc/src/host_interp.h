#ifndef GUARD_PC_HOST_INTERP_H
#define GUARD_PC_HOST_INTERP_H

/* Frame interpolation (interp.c, ppu_hd.c; docs/platforms.md, "Frame
 * interpolation"): the window shows more frames a second than the GBA's
 * 59.73 by drawing pictures between the game's last two frames. The game
 * runs as it always does, a frame at a time: nothing in it changes. Each
 * game frame's display state (the registers, VRAM, palette RAM, OAM, and
 * which game object drew each OAM entry) is kept; a picture between two
 * frames is the nearer frame's state, its BG scrolls and its objects'
 * positions moved part of the way towards the other's, drawn by an HD
 * version of the PPU (ppu_hd.c) at `scale` times the GBA's resolution, so
 * that a position between two pixels shows. Nothing is guessed: what
 * can't be matched between the frames (a new object, a scroll that
 * jumped) is drawn where the frame has it. */

#include "gba/types.h"

/* At each VBlank (sdl.c): keeps the frame just drawn. */
void HostInterpCapture(void);
/* Both frames kept (and the same mode): there is something to interpolate. */
int HostInterpReady(void);
/* Forgets the frames (after a pause, a menu, fast forward). */
void HostInterpReset(void);

/* The picture `alpha` (0: the frame before, 1: the last one) of the way,
 * `scale` times the GBA's size, in `out` (HOST_SCREEN_WIDTH * scale wide).
 * The lines are drawn by `parallel(run, ctx, lines)`, which calls
 * run(ctx, first, end) over [0, lines) on as many threads as it likes. */
typedef void (*HostLineRun)(void *ctx, int first, int end);
void HostInterpRender(double alpha, int scale, u32 *out,
                      void (*parallel)(HostLineRun run, void *ctx, int lines));

/* The OAM entries' owners (interp.c): the game's sprite drawing tells
 * which object (and where it stands) each OAM entry it adds belongs to. */
void HostInterpTagObject(const void *object, s32 x, s32 y);
void HostInterpTagNone(void);

/* --hd-check (main.c): draws every checkpoint frame through ppu_hd.c at
 * scale 1 and 2 with nothing moved and compares it with ppu.c's picture. */
extern int gHostHdCheck;
/* --interp-dump DIR --interp-at N: frames N to N+2 at 0, 1/4, 1/2 and 3/4
 * of the way from the frame before, at 4 times the size, as PNGs. */
extern const char *gHostInterpDumpDir;
extern u32 gHostInterpDumpAt;
void HostHdCheckFrame(void);
void HostHdCheckEnd(void);

#endif /* GUARD_PC_HOST_INTERP_H */
