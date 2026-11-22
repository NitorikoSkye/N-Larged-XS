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
/* Widescreen (HOST_WIDE, the screen wider than the GBA's: the setting;
 * HOST_WIDE_BG says the build can) has four
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
/* A byte: the save menu (1) or the warp room (2, HOST_WIDE_WARP_ROOM) is
 * up. Their backgrounds aren't cut to the centred art: the sky's map
 * repeats. The warp room's platforms' strip (BG1: the worlds side by side,
 * which the page turn scrolls) is, or the neighbouring worlds' platforms
 * would show beside the picture. */
#define HOST_WIDE_WARP      (HOST_WIDE_BG_EDGES + 0x21)
#define HOST_WIDE_WARP_ROOM 2
/* A byte: a vehicle stage is up (the polar ride, the jetpack, the
 * hovercraft fight: ActorCategory::Init). Its BG0 and BG1 are 304-pixel
 * art that ActorBg scrolls across for the screen's width (actor_bg.cpp's
 * maxX), so they fill the screen: neither moved to the centre nor cut. */
#define HOST_WIDE_ACTOR (HOST_WIDE_BG_EDGES + 0x22)
/* Layer 0's tile slots 0x200-0x3FF in widescreen (TileSlotPool,
 * bg_layer.hpp: the wider screen shows more of its tiles at once), 64 bytes
 * each: a wide-map 8bpp BG's tiles 0x200 on are read from here (ppu.c). */
#define HOST_WIDE_BG_TILES      (HOST_WIDE_BG_EDGES + 0x100)
#define HOST_WIDE_BG_TILE_FIRST 0x200
#define HOST_VRAM_SIZE          (HOST_WIDE_BG_TILES + 0x200 * 64)
/* the GBA's own; a checkpoint (input.c) hashes it, and at the GBA's
 * width only it, as a 240-pixel build always has */
#define HOST_VRAM_GBA_SIZE 0x18000
#define HOST_VRAM_HASHED   (HOST_WIDE ? HOST_VRAM_SIZE : HOST_VRAM_GBA_SIZE)
/* The GBA's screen width, and whether the screen is wider (the
 * widescreen setting: DISPLAY_WIDTH, gba/defines.h). */
#define HOST_GBA_WIDTH 240
#define HOST_WIDE      (DISPLAY_WIDTH != HOST_GBA_WIDTH)
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
