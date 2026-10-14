#ifndef PICKUPS_H
#define PICKUPS_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Collectible pickups (cherries and gems) for the current level.
 *
 * Each pickup is an animated BOB placed at a fixed world position (from the
 * generated level entity table). They share the game's bob manager, so the
 * lifecycle mirrors the player:
 *   pickupsLoad(pTileBuffer)   - load bitmaps from disk (before systemUnuse())
 *   bobManagerCreate(...)
 *   pickupsInitBobs()          - register each pickup's bob (before realloc)
 *   bobReallocateBuffers()
 *   ...per frame: pickupsProcess() then pickupsPushBobs()
 *   pickupsDestroy()           - free bitmaps
 */

void pickupsLoad(tTileBufferManager *pTileBuffer);

void pickupsInitBobs(void);

// Reactivate every cherry/gem, drop hearts, and restore ammo from the
// session snapshot. Does not touch bitmaps or bob registrations.
void pickupsReset(void);

// Advance the shared animation and test collection against the player hitbox.
void pickupsProcess(void);

// Push the on-screen, uncollected pickups to the bob draw queue.
void pickupsPushBobs(void);

void pickupsDestroy(void);

// Cherries remaining as spit ammo (carries between levels; a death retry
// restores the count from the start of the current level).
UWORD pickupsGetAmmo(void);

// Spend one cherry if any remain. Returns 1 if ammo was consumed.
UBYTE pickupsTrySpendAmmo(void);

// Cherries collected during the current level (not carried-in ammo).
UWORD pickupsGetCherriesCollected(void);

// Gems collected during the current level.
UWORD pickupsGetGemsCollected(void);

// Gems still uncollected in this level (0 means the exit may open).
UWORD pickupsGetGemsRemaining(void);

// True once every gem in the level has been picked up (also true if the
// level has no gems).
UBYTE pickupsAllGemsCollected(void);

// 1-in-8 chance to drop a heart pickup at the world-pixel centre.
void pickupsTryDropHeart(WORD wCenterX, WORD wCenterY);

// Activate reserved gem slots at a world-pixel centre (boss drop).
void pickupsSpawnDroppedGems(WORD wCenterX, WORD wCenterY);

#endif // PICKUPS_H
