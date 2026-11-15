#ifndef GUARD_PC_HOST_INTERP_TAGS_H
#define GUARD_PC_HOST_INTERP_TAGS_H

/* interp_tags.cpp's notes to interp.c (host_interp.h has the rest). */

#include "gba/types.h"
#include "host_interp.h"

/* An entry went to the OAM buffer's slot `slot` (Add). */
void HostInterpNoteSlot(s32 slot);
/* `n` entries went to the slots from `first` on (Append): the queue's,
 * while it is flushing. */
void HostInterpNoteAppend(s32 first, s32 n);
/* The buffer became the OAM (Commit). */
void HostInterpNoteCommit(void);
/* An entry was queued (SpriteOamQueue::Add), and the queue flushes. */
void HostInterpNoteQueued(void);
void HostInterpQueueFlushing(int on);

#endif /* GUARD_PC_HOST_INTERP_TAGS_H */
