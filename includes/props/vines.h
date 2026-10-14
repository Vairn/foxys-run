#ifndef VINES_H
#define VINES_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Tarzan pendulum vines from the .dat trailer.
 *
 * One 32x64 BOB per vine: baked lean frames, leaf at the tip. Needs
 * ACE_SCROLLBUFFER_X_MARGIN_SIZE 2 so the 32px blit fits the wrap.
 * Process runs every other frame (double step) unless the fox is hanging.
 * Player owns s_isSwinging; this module owns the pendulum and grab slot.
 *
 * Lifecycle (same bob manager as pickups/movers):
 *   vinesLoad(pTileBuffer)
 *   bobManagerCreate(...)
 *   vinesInitBobs()
 *   bobReallocateBuffers()
 *   vinesProcess() before playerProcess()
 *   vinesPushBobs()
 *   vinesDestroy()
 */

void vinesLoad(tTileBufferManager *pTileBuffer);

void vinesInitBobs(void);

void vinesReset(void);

void vinesProcess(void);

void vinesPushBobs(void);

void vinesDestroy(void);

// Auto-grab if the hitbox overlaps a knot and nothing is already grabbed.
UBYTE vinesTryGrab(WORD wLeft, WORD wTop, WORD wRight, WORD wBottom);

// 8.8 world position of the grabbed knot (no-op if none).
void vinesGrabPos(LONG *plX, LONG *plY);

// This-frame 8.8 delta of the grabbed knot (for jump-off inherit).
void vinesGrabDelta(LONG *plDx, LONG *plDy);

void vinesClearGrab(void);

#endif
