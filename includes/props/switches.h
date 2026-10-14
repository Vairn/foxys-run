#ifndef SWITCHES_H
#define SWITCHES_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Channel-linked switches (16x16 BOBs). Shoot or player-touch, one-shot or
 * toggle. Channel 1–7 broadcasts to doors and gated movers.
 *
 * Lifecycle (same bob manager as trampolines):
 *   switchesLoad(pTileBuffer)
 *   bobManagerCreate(...)
 *   switchesInitBobs()
 *   bobReallocateBuffers()
 *   ...per frame: switchesProcess() then switchesPushBobs()
 *   switchesDestroy()
 */

void switchesLoad(tTileBufferManager *pTileBuffer);

void switchesInitBobs(void);

void switchesReset(void);

void switchesProcess(void);

void switchesPushBobs(void);

void switchesDestroy(void);

// True if the pip AABB hits a shoot switch. Consumes the trigger.
UBYTE switchesHitWorldRect(WORD wX0, WORD wY0, WORD wX1, WORD wY1);

void switchesActivateChannel(UBYTE ubChannel);

void switchesDeactivateChannel(UBYTE ubChannel);

#endif /* SWITCHES_H */
