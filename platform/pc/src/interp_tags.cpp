/* Which game object drew each OAM entry, for the frame interpolation
 * (host_interp.h, interp.c). platform.mk links the game with these
 * functions --wrap'ed (HOST_WRAPS): the sprite renderer's entry points
 * tell interp.c whose pieces the OAM entries they add are, and where
 * that object stands on the screen; the OAM buffer's Add, Append and
 * Commit tell it which slot each entry went to and when the slots became
 * the OAM. Each wrapper calls the game's own function: nothing the game
 * does changes. */

#include "game/entities/sprite_obj.hpp"
#include "engine/gfx/sprite_oam_queue.hpp"

extern "C" {
#include "core.h"
#include "engine/gfx/gfx.h"
#include "host_interp_tags.h"
}

using Engine::Gfx::OamBuffer;
using Engine::Gfx::SpriteRenderer;
using Game::Entities::Sprite;

extern "C" {

void __real__ZN6Engine3Gfx9OamBuffer3AddEPKv(OamBuffer *self, const void *entry);
void __real__ZN6Engine3Gfx9OamBuffer6AppendEPvi(OamBuffer *self, void *entries, s32 n);
void __real__ZN6Engine3Gfx9OamBuffer6CommitEv(OamBuffer *self);
void __real__ZN6Engine3Gfx14SpriteRenderer4DrawEPN4Game8Entities6SpriteE(SpriteRenderer *self, Sprite *part);
void __real__ZN6Engine3Gfx14SpriteRenderer6DrawAtEPN4Game8Entities6SpriteEii(SpriteRenderer *self, Sprite *part,
                                                                                s32 x, s32 y);
void __real__ZN6Engine3Gfx14SpriteRenderer10DrawPiecesEPN4Game8Entities6SpriteEPi(SpriteRenderer *self,
                                                                                    Sprite *part, s32 *pos);
void __real__ZN6Engine3Gfx14SpriteRenderer16DrawAffinePiecesEPN4Game8Entities6SpriteEPi(SpriteRenderer *self,
                                                                                          Sprite *part, s32 *pos);
void __real__ZN6Engine3Gfx14SpriteOamQueue3AddEjti(u32 attr01, u16 attr2, s32 priority);
void __real__ZN6Engine3Gfx14SpriteOamQueue5FlushEv(void);

void __wrap__ZN6Engine3Gfx9OamBuffer3AddEPKv(OamBuffer *self, const void *entry)
{
    s32 n = self->count;

    __real__ZN6Engine3Gfx9OamBuffer3AddEPKv(self, entry);
    if (self->count > n) {
        HostInterpNoteSlot(n);
    }
}

void __wrap__ZN6Engine3Gfx9OamBuffer6AppendEPvi(OamBuffer *self, void *entries, s32 n)
{
    s32 first = self->count;

    __real__ZN6Engine3Gfx9OamBuffer6AppendEPvi(self, entries, n);
    HostInterpNoteAppend(first, n);
}

void __wrap__ZN6Engine3Gfx9OamBuffer6CommitEv(OamBuffer *self)
{
    __real__ZN6Engine3Gfx9OamBuffer6CommitEv(self);
    HostInterpNoteCommit();
}

/* The renderer's entry points, as DrawAt places a part. */
static void Anchor(Sprite *part, s32 x, s32 y, s32 *ax, s32 *ay)
{
    if (part->screenSpace == 0) {
        WorldToScreen(part, x, y, ax, ay);
    } else {
        *ax = x;
        *ay = y;
    }
}

void __wrap__ZN6Engine3Gfx14SpriteRenderer4DrawEPN4Game8Entities6SpriteE(SpriteRenderer *self, Sprite *part)
{
    s32 x;
    s32 y;

    Anchor(part, Q8_TO_INT(part->x), Q8_TO_INT(part->y), &x, &y);
    HostInterpTagObject(part, x, y);
    __real__ZN6Engine3Gfx14SpriteRenderer4DrawEPN4Game8Entities6SpriteE(self, part);
    HostInterpTagNone();
}

void __wrap__ZN6Engine3Gfx14SpriteRenderer6DrawAtEPN4Game8Entities6SpriteEii(SpriteRenderer *self, Sprite *part,
                                                                                s32 x, s32 y)
{
    s32 ax;
    s32 ay;

    Anchor(part, x, y, &ax, &ay);
    HostInterpTagObject(part, ax, ay);
    __real__ZN6Engine3Gfx14SpriteRenderer6DrawAtEPN4Game8Entities6SpriteEii(self, part, x, y);
    HostInterpTagNone();
}

void __wrap__ZN6Engine3Gfx14SpriteRenderer10DrawPiecesEPN4Game8Entities6SpriteEPi(SpriteRenderer *self,
                                                                                    Sprite *part, s32 *pos)
{
    HostInterpTagObject(part, pos[0], pos[1]);
    __real__ZN6Engine3Gfx14SpriteRenderer10DrawPiecesEPN4Game8Entities6SpriteEPi(self, part, pos);
    HostInterpTagNone();
}

void __wrap__ZN6Engine3Gfx14SpriteRenderer16DrawAffinePiecesEPN4Game8Entities6SpriteEPi(SpriteRenderer *self,
                                                                                          Sprite *part, s32 *pos)
{
    HostInterpTagObject(part, pos[0], pos[1]);
    __real__ZN6Engine3Gfx14SpriteRenderer16DrawAffinePiecesEPN4Game8Entities6SpriteEPi(self, part, pos);
    HostInterpTagNone();
}

/* The queue's entries go to the OAM buffer together at its Flush
 * (Append): each keeps the tag it was queued under. */
void __wrap__ZN6Engine3Gfx14SpriteOamQueue3AddEjti(u32 attr01, u16 attr2, s32 priority)
{
    HostInterpNoteQueued();
    __real__ZN6Engine3Gfx14SpriteOamQueue3AddEjti(attr01, attr2, priority);
}

void __wrap__ZN6Engine3Gfx14SpriteOamQueue5FlushEv(void)
{
    HostInterpQueueFlushing(1);
    __real__ZN6Engine3Gfx14SpriteOamQueue5FlushEv();
    HostInterpQueueFlushing(0);
}

} /* extern "C" */
