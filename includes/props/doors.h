#ifndef DOORS_H
#define DOORS_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Channel-linked doors: vanishing BOB, sliding BOB, or off-screen terrain
 * rewrite. Closed doors are solid for the player, shots, and monsters.
 *
 * Lifecycle (same bob manager as trampolines):
 *   doorsLoad(pTileBuffer)
 *   bobManagerCreate(...)
 *   doorsInitBobs()
 *   bobReallocateBuffers()
 *   ...per frame: doorsProcess() then doorsPushBobs()
 *   doorsDestroy()
 */

void doorsLoad(tTileBufferManager *pTileBuffer);

void doorsInitBobs(void);

void doorsReset(void);

void doorsProcess(void);

void doorsPushBobs(void);

void doorsDestroy(void);

void doorsOpenChannel(UBYTE ubChannel);

void doorsCloseChannel(UBYTE ubChannel);

UBYTE doorsBlockedVSpan(WORD wX, WORD wY0, WORD wY1);

UBYTE doorsBlockedCeiling(WORD wY, WORD wX0, WORD wX1);

// Snap edge for a probe that is inside a door. isMovingRight: door's left-1.
// Else door's right+1. -1 if the probe is not in a closed door.
WORD doorsSnapX(WORD wX, WORD wY0, WORD wY1, UBYTE isMovingRight);

// Head snap Y (door bottom + 1) or -1.
WORD doorsSnapCeiling(WORD wY, WORD wX0, WORD wX1);

WORD doorsTryLand(WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight);

UBYTE doorsProbeGround(WORD wBelow, WORD wLeft, WORD wRight);

UBYTE doorsHitWorldRect(WORD wX0, WORD wY0, WORD wX1, WORD wY1);

#endif /* DOORS_H */
