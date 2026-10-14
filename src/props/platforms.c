/* SunnyLand moving platforms — wood 48x16 bobs that ping-pong on one axis.
 *
 * Spawned from the optional .dat trailer (tLevelPlatform). Motion is 8.8
 * fixed point with a short pause at each end. The player rides them via
 * AABB queries; they are never written into the tilebuffer.
 */
#include "props/platforms.h"
#include "assets/assets.h"
#include "world/level.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include "debug/serial_log.h"
#include <ace/utils/bitmap.h>

#include <sprites/mover.h>

#define PLAT_W MOVER_FRAME_W
#define PLAT_H MOVER_FRAME_H
#define PLAT_PAUSE_FRAMES 15
#define RIDE_NONE 0xFF
#define PLAT_EVERY_OTHER_FRAME 1

typedef struct {
	tBob sBob;
	LONG lPosX;
	LONG lPosY;
	LONG lVelX;
	LONG lVelY;
	LONG lDeltaX;
	LONG lDeltaY;
	LONG lMin;
	LONG lMax;
	UBYTE isVertical;
	UBYTE ubWait;
	UBYTE isActive;
} tPlatform;

static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pMover, *s_pMoverMask;
static UBYTE *s_pFrame;
static UBYTE *s_pMask;
static tPlatform s_pPlatforms[LEVEL_MAX_PLATFORMS];
static UWORD s_uwCount;
static UBYTE s_ubRiding;
static UBYTE s_ubHalfFrame;

static void platformHome(tPlatform *pPlat, const tLevelPlatform *pDef) {
	WORD wStartX = (WORD)(pDef->uwStartX << TILE_SHIFT);
	WORD wStartY = (WORD)(pDef->uwStartY << TILE_SHIFT);
	WORD wEndX = (WORD)(pDef->uwEndX << TILE_SHIFT);
	WORD wEndY = (WORD)(pDef->uwEndY << TILE_SHIFT);
	LONG lSpeed = (LONG)pDef->uwSpeed;

	pPlat->isVertical = (pDef->uwStartX == pDef->uwEndX) ? 1 : 0;
	pPlat->lPosX = PX(wStartX);
	pPlat->lPosY = PX(wStartY);
	pPlat->lVelX = 0;
	pPlat->lVelY = 0;
	pPlat->lDeltaX = 0;
	pPlat->lDeltaY = 0;
	pPlat->ubWait = 0;
	pPlat->isActive = (pDef->uwChannel == 0) ? 1 : 0;

	if(pPlat->isVertical) {
		if(wEndY >= wStartY) {
			pPlat->lMin = PX(wStartY);
			pPlat->lMax = PX(wEndY);
			pPlat->lVelY = lSpeed;
		}
		else {
			pPlat->lMin = PX(wEndY);
			pPlat->lMax = PX(wStartY);
			pPlat->lVelY = -lSpeed;
		}
	}
	else {
		if(wEndX >= wStartX) {
			pPlat->lMin = PX(wStartX);
			pPlat->lMax = PX(wEndX);
			pPlat->lVelX = lSpeed;
		}
		else {
			pPlat->lMin = PX(wEndX);
			pPlat->lMax = PX(wStartX);
			pPlat->lVelX = -lSpeed;
		}
	}
}

static void platformSyncBob(tPlatform *pPlat) {
	WORD wX = TO_PX(pPlat->lPosX);
	WORD wY = TO_PX(pPlat->lPosY);
	if(wX < 0) {
		wX = 0;
	}
	if(wY < 0) {
		wY = 0;
	}
	pPlat->sBob.sPos.uwX = (UWORD)wX;
	pPlat->sBob.sPos.uwY = (UWORD)wY;
}

static UBYTE platformOverlapsX(const tPlatform *pPlat, WORD wLeft, WORD wRight) {
	WORD wPlatL = TO_PX(pPlat->lPosX);
	WORD wPlatR = wPlatL + PLAT_W - 1;
	return (wPlatL <= wRight && wPlatR >= wLeft) ? 1 : 0;
}

void platformsLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_pMover = 0;
	s_pMoverMask = 0;
	s_pFrame = 0;
	s_pMask = 0;
	s_uwCount = 0;
	s_ubRiding = RIDE_NONE;

	if(!levelGetPlatformCount()) {
		return;
	}
	s_pMover = sunnyLoadBitmap("data/sprites/mover.bm", 0);
	s_pMoverMask = sunnyLoadBitmap("data/sprites/mover_mask.bm", 0);
	if(s_pMover) {
		s_pFrame = bobCalcFrameAddress(s_pMover, 0);
	}
	if(s_pMoverMask) {
		s_pMask = bobCalcFrameAddress(s_pMoverMask, 0);
	}
}

void platformsInitBobs(void) {
	s_uwCount = levelGetPlatformCount();
	if(s_uwCount > LEVEL_MAX_PLATFORMS) {
		s_uwCount = LEVEL_MAX_PLATFORMS;
	}
	s_ubRiding = RIDE_NONE;
	if(!s_uwCount || !s_pFrame || !s_pMask) {
		s_uwCount = 0;
		return;
	}

	const tLevelPlatform *pDefs = levelGetPlatforms();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tPlatform *pPlat = &s_pPlatforms[i];
		platformHome(pPlat, &pDefs[i]);
		bobInit(
			&pPlat->sBob, PLAT_W, PLAT_H, 1,
			s_pFrame, s_pMask,
			(UWORD)TO_PX(pPlat->lPosX), (UWORD)TO_PX(pPlat->lPosY)
		);
	}
}

void platformsReset(void) {
	s_ubRiding = RIDE_NONE;
	s_ubHalfFrame = 0;
	if(!s_uwCount) {
		return;
	}
	const tLevelPlatform *pDefs = levelGetPlatforms();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		platformHome(&s_pPlatforms[i], &pDefs[i]);
		platformSyncBob(&s_pPlatforms[i]);
	}
}

void platformsProcess(void) {
	if(PLAT_EVERY_OTHER_FRAME) {
		s_ubHalfFrame ^= 1;
	}

	for(UWORD i = 0; i < s_uwCount; ++i) {
		tPlatform *pPlat = &s_pPlatforms[i];
		LONG lOldX = pPlat->lPosX;
		LONG lOldY = pPlat->lPosY;
		UBYTE isRidden = (s_ubRiding == (UBYTE)i) ? 1 : 0;
		UBYTE isDouble = 0;

		if(PLAT_EVERY_OTHER_FRAME && !isRidden && !s_ubHalfFrame) {
			pPlat->lDeltaX = 0;
			pPlat->lDeltaY = 0;
			continue;
		}
		if(PLAT_EVERY_OTHER_FRAME && !isRidden) {
			isDouble = 1;
		}

		if(!pPlat->isActive) {
			pPlat->lDeltaX = 0;
			pPlat->lDeltaY = 0;
			continue;
		}

		if(pPlat->ubWait) {
			if(isDouble && pPlat->ubWait > 1) {
				pPlat->ubWait = (UBYTE)(pPlat->ubWait - 2);
			}
			else {
				--pPlat->ubWait;
			}
			pPlat->lDeltaX = 0;
			pPlat->lDeltaY = 0;
			continue;
		}

		if(pPlat->isVertical) {
			pPlat->lPosY += isDouble ? (pPlat->lVelY << 1) : pPlat->lVelY;
			if(pPlat->lVelY > 0 && pPlat->lPosY >= pPlat->lMax) {
				pPlat->lPosY = pPlat->lMax;
				pPlat->lVelY = -pPlat->lVelY;
				pPlat->ubWait = PLAT_PAUSE_FRAMES;
			}
			else if(pPlat->lVelY < 0 && pPlat->lPosY <= pPlat->lMin) {
				pPlat->lPosY = pPlat->lMin;
				pPlat->lVelY = -pPlat->lVelY;
				pPlat->ubWait = PLAT_PAUSE_FRAMES;
			}
		}
		else {
			pPlat->lPosX += isDouble ? (pPlat->lVelX << 1) : pPlat->lVelX;
			if(pPlat->lVelX > 0 && pPlat->lPosX >= pPlat->lMax) {
				pPlat->lPosX = pPlat->lMax;
				pPlat->lVelX = -pPlat->lVelX;
				pPlat->ubWait = PLAT_PAUSE_FRAMES;
			}
			else if(pPlat->lVelX < 0 && pPlat->lPosX <= pPlat->lMin) {
				pPlat->lPosX = pPlat->lMin;
				pPlat->lVelX = -pPlat->lVelX;
				pPlat->ubWait = PLAT_PAUSE_FRAMES;
			}
		}

		pPlat->lDeltaX = pPlat->lPosX - lOldX;
		pPlat->lDeltaY = pPlat->lPosY - lOldY;
		platformSyncBob(pPlat);
	}
}

void platformsPushBobs(void) {
	if(!s_uwCount || !s_pTileBuffer) {
		return;
	}
	UWORD uwCamX = s_pTileBuffer->pCamera->uPos.uwX;
	UWORD uwCamY = s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewL = (WORD)uwCamX - CULL_MARGIN;
	WORD wViewT = (WORD)uwCamY - CULL_MARGIN;
	WORD wViewR = (WORD)uwCamX + SCREEN_W + CULL_MARGIN;
	WORD wViewB = (WORD)uwCamY + SCREEN_H + CULL_MARGIN;

	for(UWORD i = 0; i < s_uwCount; ++i) {
		tPlatform *pPlat = &s_pPlatforms[i];
		WORD wX = (WORD)pPlat->sBob.sPos.uwX;
		WORD wY = (WORD)pPlat->sBob.sPos.uwY;
		if(
			wX > wViewR || wY > wViewB ||
			(wX + PLAT_W) < wViewL || (wY + PLAT_H) < wViewT
		) {
			continue;
		}
		bobPush(&pPlat->sBob);
	}
}

void platformsDestroy(void) {
	if(s_pMover) {
		bitmapDestroy(s_pMover);
		s_pMover = 0;
	}
	if(s_pMoverMask) {
		bitmapDestroy(s_pMoverMask);
		s_pMoverMask = 0;
	}
	s_pFrame = 0;
	s_pMask = 0;
	s_uwCount = 0;
	s_ubRiding = RIDE_NONE;
}

void platformsClearRide(void) {
	s_ubRiding = RIDE_NONE;
}

UBYTE platformsIsRiding(void) {
	return (s_ubRiding < s_uwCount) ? 1 : 0;
}

UBYTE platformsStickRide(WORD wLeft, WORD wRight, WORD *pwSurf) {
	if(s_ubRiding >= s_uwCount) {
		return 0;
	}
	tPlatform *pPlat = &s_pPlatforms[s_ubRiding];
	if(!platformOverlapsX(pPlat, wLeft, wRight)) {
		s_ubRiding = RIDE_NONE;
		return 0;
	}
	if(pwSurf) {
		*pwSurf = TO_PX(pPlat->lPosY);
	}
	return 1;
}

LONG platformsRideDeltaX(void) {
	if(s_ubRiding >= s_uwCount) {
		return 0;
	}
	return s_pPlatforms[s_ubRiding].lDeltaX;
}

LONG platformsRideDeltaY(void) {
	if(s_ubRiding >= s_uwCount) {
		return 0;
	}
	return s_pPlatforms[s_ubRiding].lDeltaY;
}

LONG platformsRideVelX(void) {
	if(s_ubRiding >= s_uwCount) {
		return 0;
	}
	return s_pPlatforms[s_ubRiding].lVelX;
}

WORD platformsTryLand(WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight) {
	WORD wBest = -1;
	UBYTE ubBest = RIDE_NONE;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tPlatform *pPlat = &s_pPlatforms[i];
		WORD wTop = TO_PX(pPlat->lPosY);
		// One-way: catch a downward crossing, or feet already in the 1px gap
		// above the top. A descending mover would otherwise slip away because
		// gravity is only 0.5px/frame. Ignore if we are already well through.
		if(wNewBottom + 1 < wTop) {
			continue;
		}
		if(wOldBottom >= wTop + 2) {
			continue;
		}
		if(!platformOverlapsX(pPlat, wLeft, wRight)) {
			continue;
		}
		if(wBest < 0 || wTop < wBest) {
			wBest = wTop;
			ubBest = (UBYTE)i;
		}
	}
	if(ubBest != RIDE_NONE) {
		s_ubRiding = ubBest;
	}
	return wBest;
}

UBYTE platformsProbeGround(WORD wBelow, WORD wLeft, WORD wRight) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tPlatform *pPlat = &s_pPlatforms[i];
		WORD wTop = TO_PX(pPlat->lPosY);
		WORD wDiff = (WORD)(wBelow - wTop);
		// Exact match plus a few px of slop so a vertical mover's 8.8 step
		// cannot desync the foot probe from the bob.
		if(wDiff < (WORD)-3 || wDiff > 4) {
			continue;
		}
		if(!platformOverlapsX(pPlat, wLeft, wRight)) {
			continue;
		}
		s_ubRiding = (UBYTE)i;
		return 1;
	}
	return 0;
}

void platformsActivateChannel(UBYTE ubChannel) {
	if(!ubChannel) {
		return;
	}
	const tLevelPlatform *pDefs = levelGetPlatforms();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		if(pDefs[i].uwChannel == ubChannel) {
			s_pPlatforms[i].isActive = 1;
		}
	}
}

void platformsDeactivateChannel(UBYTE ubChannel) {
	if(!ubChannel) {
		return;
	}
	const tLevelPlatform *pDefs = levelGetPlatforms();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		if(pDefs[i].uwChannel == ubChannel) {
			s_pPlatforms[i].isActive = 0;
			s_pPlatforms[i].lDeltaX = 0;
			s_pPlatforms[i].lDeltaY = 0;
		}
	}
}
