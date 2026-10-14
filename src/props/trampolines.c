/* SunnyLand trampolines — 32x16 bounce pads from the .dat trailer. */
#include "props/trampolines.h"
#include "assets/assets.h"
#include "world/level.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include <ace/utils/bitmap.h>

#include <sprites/trampoline.h>

#define TRAMP_W TRAMPOLINE_FRAME_W
#define TRAMP_H TRAMPOLINE_FRAME_H
#define SQUASH_FRAMES 8

typedef struct {
	tBob sBob;
	WORD wX;
	WORD wY;
	UBYTE ubSquash;
} tTrampoline;

static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pBm, *s_pMaskBm;
static UBYTE *s_pFrame[TRAMPOLINE_FRAME_COUNT];
static UBYTE *s_pMask[TRAMPOLINE_FRAME_COUNT];
static tTrampoline s_pTramps[LEVEL_MAX_TRAMPOLINES];
static UWORD s_uwCount;

static UBYTE trampOverlapsX(const tTrampoline *pPad, WORD wLeft, WORD wRight) {
	WORD wPadR = pPad->wX + TRAMP_W - 1;
	return (pPad->wX <= wRight && wPadR >= wLeft) ? 1 : 0;
}

static void trampSetFrame(tTrampoline *pPad, UBYTE ubFrame) {
	if(ubFrame >= TRAMPOLINE_FRAME_COUNT) {
		ubFrame = 0;
	}
	bobSetFrame(&pPad->sBob, s_pFrame[ubFrame], s_pMask[ubFrame]);
}

void trampolinesLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_pBm = 0;
	s_pMaskBm = 0;
	s_uwCount = 0;
	for(UBYTE i = 0; i < TRAMPOLINE_FRAME_COUNT; ++i) {
		s_pFrame[i] = 0;
		s_pMask[i] = 0;
	}

	if(!levelGetTrampolineCount()) {
		return;
	}
	/* Default drawn pad. Pack convert also writes mushroom_small.bm (32x39);
	 * assign that sheet only from a per-level tramp override, not globally. */
	s_pBm = sunnyLoadBitmap("data/sprites/trampoline.bm", 0);
	s_pMaskBm = sunnyLoadBitmap("data/sprites/trampoline_mask.bm", 0);
	if(!s_pBm || !s_pMaskBm) {
		return;
	}
	for(UBYTE i = 0; i < TRAMPOLINE_FRAME_COUNT; ++i) {
		s_pFrame[i] = bobCalcFrameAddress(s_pBm, i * TRAMP_H);
		s_pMask[i] = bobCalcFrameAddress(s_pMaskBm, i * TRAMP_H);
	}
}

void trampolinesInitBobs(void) {
	s_uwCount = levelGetTrampolineCount();
	if(s_uwCount > LEVEL_MAX_TRAMPOLINES) {
		s_uwCount = LEVEL_MAX_TRAMPOLINES;
	}
	if(!s_uwCount || !s_pFrame[0] || !s_pMask[0]) {
		s_uwCount = 0;
		return;
	}

	const tLevelTrampoline *pDefs = levelGetTrampolines();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tTrampoline *pPad = &s_pTramps[i];
		pPad->wX = (WORD)(pDefs[i].uwTileX << TILE_SHIFT);
		pPad->wY = (WORD)(pDefs[i].uwTileY << TILE_SHIFT);
		pPad->ubSquash = 0;
		if(pPad->wX < 0) {
			pPad->wX = 0;
		}
		if(pPad->wY < 0) {
			pPad->wY = 0;
		}
		bobInit(
			&pPad->sBob, TRAMP_W, TRAMP_H, 1,
			s_pFrame[0], s_pMask[0],
			(UWORD)pPad->wX, (UWORD)pPad->wY
		);
	}
}

void trampolinesReset(void) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		s_pTramps[i].ubSquash = 0;
		if(s_pFrame[0]) {
			trampSetFrame(&s_pTramps[i], 0);
		}
	}
}

void trampolinesProcess(void) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tTrampoline *pPad = &s_pTramps[i];
		if(pPad->ubSquash) {
			--pPad->ubSquash;
			if(!pPad->ubSquash) {
				trampSetFrame(pPad, 0);
			}
		}
	}
}

void trampolinesPushBobs(void) {
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
		tTrampoline *pPad = &s_pTramps[i];
		if(
			pPad->wX > wViewR || pPad->wY > wViewB ||
			(pPad->wX + TRAMP_W) < wViewL || (pPad->wY + TRAMP_H) < wViewT
		) {
			continue;
		}
		bobPush(&pPad->sBob);
	}
}

void trampolinesDestroy(void) {
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
	if(s_pMaskBm) {
		bitmapDestroy(s_pMaskBm);
		s_pMaskBm = 0;
	}
	s_uwCount = 0;
}

WORD trampolinesTryBounce(
	WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight
) {
	WORD wBest = -1;
	UBYTE ubBest = 0xFF;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tTrampoline *pPad = &s_pTramps[i];
		WORD wTop = pPad->wY;
		if(wNewBottom + 1 < wTop) {
			continue;
		}
		if(wOldBottom >= wTop + 2) {
			continue;
		}
		if(!trampOverlapsX(pPad, wLeft, wRight)) {
			continue;
		}
		if(wBest < 0 || wTop < wBest) {
			wBest = wTop;
			ubBest = (UBYTE)i;
		}
	}
	if(ubBest != 0xFF) {
		s_pTramps[ubBest].ubSquash = SQUASH_FRAMES;
		trampSetFrame(&s_pTramps[ubBest], 1);
	}
	return wBest;
}
