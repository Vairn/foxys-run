#ifndef SHOTS_H
#define SHOTS_H

#include <ace/types.h>
#include <ace/utils/extview.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Cherry-pip projectiles. Up to two concurrent shots, each a hardware sprite
 * on a free channel (4 and 5) so they sit above the playfield without using
 * the bob manager. Pickup cherries are ammo; fire is playerWantsFire().
 *
 * Lifecycle (around the HUD, which owns the sprite manager):
 *   hudCreate(pView)
 *   shotsCreate(pView, pTileBuffer)
 *   ...per frame: shotsProcess() then hudProcess() before copProcessBlocks()
 *   shotsDestroy()
 *   hudDestroy()
 */

void shotsCreate(tView *pView, tTileBufferManager *pTileBuffer);

// Despawn any in-flight pips. Sprites stay allocated.
void shotsReset(void);

// Spawn on fire, integrate the arc, collide, and refresh sprite channels 4–5.
void shotsProcess(void);

void shotsDestroy(void);

#endif // SHOTS_H
