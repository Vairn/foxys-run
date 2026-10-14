#ifndef TRAMPOLINES_H
#define TRAMPOLINES_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Mario-style bounce pads (32x16 BOBs) from the .dat trailer.
 *
 * Land-from-above only. Side bumps do nothing. Monsters and pips ignore them.
 *
 * Lifecycle (same bob manager as pickups/movers):
 *   trampolinesLoad(pTileBuffer)
 *   bobManagerCreate(...)
 *   trampolinesInitBobs()
 *   bobReallocateBuffers()
 *   ...per frame: trampolinesProcess() then trampolinesPushBobs()
 *   trampolinesDestroy()
 */

void trampolinesLoad(tTileBufferManager *pTileBuffer);

void trampolinesInitBobs(void);

void trampolinesReset(void);

void trampolinesProcess(void);

void trampolinesPushBobs(void);

void trampolinesDestroy(void);

// If falling feet cross a pad top, squash it and return that top Y. Else -1.
WORD trampolinesTryBounce(WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight);

#endif
