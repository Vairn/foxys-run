#ifndef EFFECTS_H
#define EFFECTS_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Transient one-shot visual effects (enemy death poof, pickup sparkle).
 *
 * Effects are short animated BOBs drawn once at a world position and then
 * retired. They live in small fixed pools and share the game's bob manager,
 * mirroring the player/pickups/monsters lifecycle:
 *   effectsLoad(pTileBuffer)  - load bitmaps from disk (before systemUnuse())
 *   bobManagerCreate(...)
 *   effectsInitBobs()         - register the pool bobs (before realloc)
 *   bobReallocateBuffers()
 *   ...per frame: effectsProcess() then effectsPushBobs()
 *   effectsDestroy()          - free bitmaps
 *
 * Spawns take a WORLD-space centre (pixels); the effect is centred there.
 */

void effectsLoad(tTileBufferManager *pTileBuffer);

void effectsInitBobs(void);

// Retire every active poof/sparkle. Does not touch bitmaps or bob registrations.
void effectsReset(void);

void effectsProcess(void);

void effectsPushBobs(void);

void effectsDestroy(void);

// Spawn an enemy death poof centred on (wCenterX, wCenterY).
void effectsSpawnDeath(WORD wCenterX, WORD wCenterY);

// Spawn a pickup-collected sparkle centred on (wCenterX, wCenterY).
void effectsSpawnPickup(WORD wCenterX, WORD wCenterY);

#endif // EFFECTS_H
