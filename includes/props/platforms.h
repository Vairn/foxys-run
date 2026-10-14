#ifndef PLATFORMS_H
#define PLATFORMS_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Rideable moving platforms (wood 48x16 bobs).
 *
 * Ping-pong between start/end tiles on one axis. Player-only one-way
 * stand-on-top collision; monsters and shots ignore them.
 *
 * Lifecycle (same bob manager as pickups/monsters):
 *   platformsLoad(pTileBuffer)  - bitmaps from disk (before systemUnuse())
 *   bobManagerCreate(...)
 *   platformsInitBobs()         - register each mover (before realloc)
 *   bobReallocateBuffers()
 *   ...per frame: platformsProcess() then platformsPushBobs()
 * Process is every other frame (double step) unless the fox is riding.
 *   platformsDestroy()
 */

void platformsLoad(tTileBufferManager *pTileBuffer);

void platformsInitBobs(void);

// Snap every mover back to its start tile and clear the ride. No disk IO.
void platformsReset(void);

// Advance ping-pong motion and remember this frame's carry delta.
void platformsProcess(void);

void platformsPushBobs(void);

void platformsDestroy(void);

void platformsClearRide(void);

UBYTE platformsIsRiding(void);

// 8.8 carry of the platform the player was riding last frame (0 if none).
LONG platformsRideDeltaX(void);

LONG platformsRideDeltaY(void);

// 8.8 X velocity of the ridden platform (for jump inherit).
LONG platformsRideVelX(void);

// If still overlapping the ridden mover, write its top Y and return 1.
// Walking off the sides clears the ride.
UBYTE platformsStickRide(WORD wLeft, WORD wRight, WORD *pwSurf);

// Land if falling feet cross (or rest on) a mover top. Returns surface Y, or -1.
// On a hit, marks that platform as the current ride.
WORD platformsTryLand(WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight);

// True if the pixel row just below the feet sits on a mover top.
// Marks that platform as the current ride when it returns 1.
UBYTE platformsProbeGround(WORD wBelow, WORD wLeft, WORD wRight);

void platformsActivateChannel(UBYTE ubChannel);

void platformsDeactivateChannel(UBYTE ubChannel);

#endif /* PLATFORMS_H */
