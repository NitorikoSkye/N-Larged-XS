#ifndef GUARD_PC_GBA_HOST_H
#define GUARD_PC_GBA_HOST_H

/* The pc platform's host memory (platform/pc/src/memory.c): the arrays
 * its gba/io_reg.h and gba/defines.h point the GBA's memory map at, and
 * the DMA call its gba/dma_macros.h makes (docs/platforms.md). */

/* The sizes are the GBA's, except the two work RAMs: the linker-placed
 * IWRAM globals sit at twice their GBA offset (sym_iwram.txt, laid out by
 * platform/pc/tools/host_link.py), so a struct that holds pointers has the
 * room it takes on a 64-bit host, and the heaps (memory.cpp, EWRAM_SIZE)
 * get twice the room too. */
#define HOST_IO_SIZE    0x400
#define HOST_EWRAM_SIZE 0x80000
#define HOST_IWRAM_SIZE 0x10000
#define HOST_PLTT_SIZE  0x400
/* A widescreen build (platform.mk's HOST_WIDE, HOST_WIDE_BG) has four
 * 64x32-entry BG maps after the GBA's 96 KB of VRAM, one for each level
 * layer (BgLayer, bg_layer.hpp): a screen wider than a 32x32 screen block
 * and a tile, with no room for 512-pixel maps in the 64 KB the levels
 * fill. A text BG whose BGnCNT has HOST_BGCNT_WIDE_MAP (bit 13, the
 * affine BGs' wraparound, which text BGs don't have) reads its map from
 * there (ppu.c, ppu_hd.c). */
#define HOST_WIDE_BG_MAPS   0x18000
#define HOST_WIDE_BG_MAP    0x1000 /* bytes a map: 64 x 32 entries */
#define HOST_BGCNT_WIDE_MAP 0x2000
/* After the maps, each layer's map on the screen, from its left edge to
 * its right (two s16 a layer, from its scroll: BgLayer::CommitScroll): a
 * room narrower than the screen shows nothing past its edges. */
#define HOST_WIDE_BG_EDGES (HOST_WIDE_BG_MAPS + 4 * HOST_WIDE_BG_MAP)
/* A byte: the game's pause menu is up, its 240-pixel art widened to the
 * screen (HostWideArtColumn, host_platform.h) rather than centred. */
#define HOST_WIDE_PAUSE (HOST_WIDE_BG_EDGES + 0x20)
#ifdef HOST_WIDE_BG
#define HOST_VRAM_SIZE (HOST_WIDE_BG_EDGES + 0x100)
#else
#define HOST_VRAM_SIZE 0x18000
#endif
#define HOST_OAM_SIZE   0x400
#define HOST_NULL_PAGE_SIZE 0x1000

/* memory.S includes the sizes. */
#ifndef __ASSEMBLER__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char gHostIo[HOST_IO_SIZE];
extern unsigned char gHostEwram[HOST_EWRAM_SIZE];
extern unsigned char gHostIwram[HOST_IWRAM_SIZE];
extern unsigned char gHostPltt[HOST_PLTT_SIZE];
extern unsigned char gHostVram[HOST_VRAM_SIZE];
extern unsigned char gHostOam[HOST_OAM_SIZE];
/* What a NULL_READ of a NULL pointer reads (gba/defines.h): zeros. */
extern unsigned char gHostNullPage[HOST_NULL_PAGE_SIZE];
/* Where a NULL_WRITE through a NULL pointer writes (gba/defines.h): never read. */
extern unsigned char gHostNullSink[HOST_NULL_PAGE_SIZE];

/* DmaSet: writes the channel's registers and, for an immediate transfer,
 * does it at once (platform/pc/src/dma.c). */
void HostDmaSet(int channel, uintptr_t src, uintptr_t dest, uint32_t control);

/* REG_KEYINPUT: the keys (active low) of this game frame
 * (platform/pc/src/input.c). */
uint16_t HostReadKeys(void);

/* REG_SIOCNT: the register, its read-only bits those of a GBA with no
 * cable attached (platform/pc/src/sio.c). */
volatile uint16_t *HostSioCnt(void);

#ifdef __cplusplus
}
#endif

#endif /* !__ASSEMBLER__ */

#endif /* GUARD_PC_GBA_HOST_H */
