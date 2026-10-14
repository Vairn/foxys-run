#ifndef SUNNY_OVERLAY_H
#define SUNNY_OVERLAY_H

#include <ace/types.h>
#include <ace/utils/bitmap.h>

struct tTileBufferManager;

void overlayCreate(void);

void overlayDestroy(void);

// Snapshot the level stats and start the bouncing "LEVEL COMPLETE" card.
void overlayBeginComplete(UWORD uwCherries, UWORD uwGems, UWORD uwKills);

// Bake a static sign card from the ROM line for ubId. Dismiss does not finish.
void overlayBeginSign(UBYTE ubId);

UBYTE overlayIsActive(void);

// Returns 1 once the complete card has been shown long enough (or skipped).
UBYTE overlayIsFinished(void);

void overlayProcess(void);

// Stamp the card onto the current scroll back-buffer at view-centre.
void overlayDraw(
	tBitMap *pBack, UWORD uwCamX, UWORD uwCamY, UWORD uwAvailH
);

// Redraw only the tiles under the card on both scroll buffers.
void overlayRepair(struct tTileBufferManager *pTiles);

#endif // SUNNY_OVERLAY_H
