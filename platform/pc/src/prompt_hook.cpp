/* Button prompts (docs/platforms.md, "Button prompts"; Controls' Button
 * prompts): the small font's A, B, L and R glyphs (gSmallFontChars'
 * last four, the GBA's buttons) are drawn as the buttons they're bound
 * to on the pad in use (prompts.c).
 *
 * The link wraps Font::DrawGlyph (platform.mk, HOST_WRAPS; it is
 * virtual, and font.o's vtable is where it is called from). It draws the
 * glyph as always, then notes where the font's button glyphs are in OBJ
 * VRAM and with which palette, so the PPUs (ppu.c, ppu_hd.c) know an OBJ
 * of those tiles in that palette is a button glyph. */

#include "engine/text/font.hpp"

extern "C" {
#include "core.h"
#include "engine/text/text.h"
#include "host_platform.h"
}

using Engine::Text::Font;

extern "C" {

void HostPromptGlyphDrawn(u32 firstTile, u32 palette); /* prompts.c */

void __real__ZN6Engine4Text4Font9DrawGlyphEh(Font *self, u8 charByte);

void __wrap__ZN6Engine4Text4Font9DrawGlyphEh(Font *self, u8 charByte)
{
    __real__ZN6Engine4Text4Font9DrawGlyphEh(self, charByte);
    if (self->glyphRecords == gSmallFontGlyphs &&
        (charByte == 'A' || charByte == 'B' || charByte == 'L' || charByte == 'R')) {
        /* The four are the font's last glyphs, in that order. */
        HostPromptGlyphDrawn(self->tileBase + self->charLookup['A'] * self->glyphTileStride,
                             self->oam_scratch.palette);
    }
}
}
