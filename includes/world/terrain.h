#ifndef TERRAIN_H
#define TERRAIN_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Crumbling and single-use walk-tops.
 *
 * Ice / conveyor / quicksand are collision bytes only. These two need
 * per-cell timers and a restamp. Scan g_ubColMap after collisionInit().
 *
 *   terrainInit(pTileBuffer)   after collisionInit
 *   terrainProcess()           in gameSimulate (CPU only)
 *   terrainFlush()             in gameDraw, before bobBegin
 *   terrainReset()             on retry
 *   terrainDestroy()
 */

void terrainInit(tTileBufferManager *pTileBuffer);

void terrainProcess(void);

void terrainFlush(void);

void terrainReset(void);

void terrainDestroy(void);

#endif /* TERRAIN_H */
