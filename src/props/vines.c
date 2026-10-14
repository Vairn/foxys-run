/* SunnyLand swinging vines — one pendulum BOB from the .dat trailer. */
#include "props/vines.h"
#include "assets/assets.h"
#include "world/level.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include <ace/utils/bitmap.h>

#include <sprites/vine.h>

#define VINE_W VINE_FRAME_W
#define VINE_H VINE_FRAME_H
#define GRAB_NONE 0xFF
#define GRAB_HALF 10
#define VINE_AMP 32 /* ±45° in 256-circle units */
#define VINE_LEAF_Y 58
#define VINES_EVERY_OTHER_FRAME 1

typedef struct {
	tBob sBob;
	LONG lAnchorX;
	LONG lAnchorY;
	LONG lLength;
	LONG lGrabX;
	LONG lGrabY;
	LONG lPendX;
	LONG lPendY;
	LONG lDeltaX;
	LONG lDeltaY;
	UWORD uwLengthTiles;
	UBYTE ubPhase;
	UBYTE ubStep;
	UBYTE ubFrame;
} tVine;

static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pBm, *s_pMaskBm;
static UBYTE *s_pFrame[VINE_FRAME_COUNT];
static UBYTE *s_pMask[VINE_FRAME_COUNT];
static tVine s_pVines[LEVEL_MAX_VINES];
static UWORD s_uwCount;
static UBYTE s_ubGrabbed;
static UBYTE s_ubHalfFrame;

/* Leaf X in the 32-wide frame, one per baked lean. */
static const BYTE s_pLeafDx[VINE_FRAME_COUNT] = {
	-14, -10, -7, -3, 0, 3, 7, 10, 14
};
_Static_assert(VINE_FRAME_COUNT == 9, "leaf dx table matches baked leans");

/* sin(i * 90/64 deg) in 8.8, i = 0..64 */
static const UWORD s_pSin90[65] = {
	0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92,
	98, 104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177,
	181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
	237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256,
	256
};

static WORD sin256(UBYTE ubAngle) {
	UBYTE ubQ = (UBYTE)(ubAngle >> 6);
	UBYTE ubT = (UBYTE)(ubAngle & 63);
	UWORD uw = s_pSin90[(ubQ & 1) ? (64 - ubT) : ubT];
	return (ubQ & 2) ? (WORD)-(WORD)uw : (WORD)uw;
}

static WORD cos256(UBYTE ubAngle) {
	return sin256((UBYTE)(ubAngle + 64));
}

static UBYTE vineFrameFromTheta(WORD wTheta) {
	/* VINE_AMP is ±32. Nine frames, ~7 units each. */
	if(wTheta <= -28) {
		return 0;
	}
	if(wTheta <= -20) {
		return 1;
	}
	if(wTheta <= -12) {
		return 2;
	}
	if(wTheta <= -4) {
		return 3;
	}
	if(wTheta <= 4) {
		return 4;
	}
	if(wTheta <= 12) {
		return 5;
	}
	if(wTheta <= 20) {
		return 6;
	}
	if(wTheta <= 28) {
		return 7;
	}
	return 8;
}

static void vineSetBobPos(tVine *pVine) {
	WORD wBobX = TO_PX(pVine->lAnchorX) - (VINE_W / 2);
	WORD wBobY = TO_PX(pVine->lAnchorY);
	if(wBobX < 0) {
		wBobX = 0;
	}
	if(wBobY < 0) {
		wBobY = 0;
	}
	pVine->sBob.sPos.uwX = (UWORD)wBobX;
	pVine->sBob.sPos.uwY = (UWORD)wBobY;
}

static void vineHome(tVine *pVine, const tLevelVine *pDef, UBYTE ubPhase) {
	UWORD uwLen = pDef->uwLength;
	if(uwLen < VINE_LENGTH_MIN) {
		uwLen = VINE_LENGTH_DEFAULT;
	}
	if(uwLen > VINE_LENGTH_MAX) {
		uwLen = VINE_LENGTH_MAX;
	}
	pVine->uwLengthTiles = uwLen;
	pVine->lAnchorX = PX((pDef->uwAnchorX << TILE_SHIFT) + (TILE_SIZE / 2));
	pVine->lAnchorY = PX(pDef->uwAnchorY << TILE_SHIFT);
	pVine->lLength = PX(uwLen << TILE_SHIFT);
	pVine->ubPhase = ubPhase;
	pVine->ubStep = (UBYTE)((uwLen <= 4) ? 3 : 2);
	pVine->lPendX = pVine->lAnchorX;
	pVine->lPendY = pVine->lAnchorY + pVine->lLength;
	pVine->lGrabX = pVine->lAnchorX;
	pVine->lGrabY = pVine->lAnchorY + PX(VINE_LEAF_Y);
	pVine->lDeltaX = 0;
	pVine->lDeltaY = 0;
	pVine->ubFrame = 4;
}

static void vineAdvance(tVine *pVine, UBYTE ubStepMul) {
	pVine->ubPhase = (UBYTE)(pVine->ubPhase + pVine->ubStep * ubStepMul);
	WORD wSwing = sin256(pVine->ubPhase);
	WORD wTheta = (WORD)((wSwing * VINE_AMP) >> 8);
	UBYTE ubTheta = (UBYTE)wTheta;
	WORD wSin = sin256(ubTheta);
	WORD wCos = cos256(ubTheta);
	LONG lPendX = pVine->lAnchorX + ((pVine->lLength * wSin) >> 8);
	LONG lPendY = pVine->lAnchorY + ((pVine->lLength * wCos) >> 8);
	pVine->lDeltaX = lPendX - pVine->lPendX;
	pVine->lDeltaY = lPendY - pVine->lPendY;
	pVine->lPendX = lPendX;
	pVine->lPendY = lPendY;

	UBYTE ubFrame = vineFrameFromTheta(wTheta);
	if(ubFrame != pVine->ubFrame) {
		pVine->ubFrame = ubFrame;
		bobSetFrame(&pVine->sBob, s_pFrame[ubFrame], s_pMask[ubFrame]);
	}

	pVine->lGrabX = pVine->lAnchorX + PX(s_pLeafDx[ubFrame]);
	pVine->lGrabY = pVine->lAnchorY + PX(VINE_LEAF_Y);
	vineSetBobPos(pVine);
}

void vinesLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_pBm = 0;
	s_pMaskBm = 0;
	s_uwCount = 0;
	s_ubGrabbed = GRAB_NONE;
	for(UBYTE i = 0; i < VINE_FRAME_COUNT; ++i) {
		s_pFrame[i] = 0;
		s_pMask[i] = 0;
	}

	if(!levelGetVineCount()) {
		return;
	}
	s_pBm = sunnyLoadBitmap("data/sprites/vine.bm", 0);
	s_pMaskBm = sunnyLoadBitmap("data/sprites/vine_mask.bm", 0);
	if(!s_pBm || !s_pMaskBm) {
		return;
	}
	for(UBYTE i = 0; i < VINE_FRAME_COUNT; ++i) {
		s_pFrame[i] = bobCalcFrameAddress(s_pBm, i * VINE_H);
		s_pMask[i] = bobCalcFrameAddress(s_pMaskBm, i * VINE_H);
	}
}

void vinesInitBobs(void) {
	s_uwCount = levelGetVineCount();
	if(s_uwCount > LEVEL_MAX_VINES) {
		s_uwCount = LEVEL_MAX_VINES;
	}
	s_ubGrabbed = GRAB_NONE;
	if(!s_uwCount || !s_pFrame[4] || !s_pMask[4]) {
		s_uwCount = 0;
		return;
	}

	const tLevelVine *pDefs = levelGetVines();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tVine *pVine = &s_pVines[i];
		vineHome(pVine, &pDefs[i], (UBYTE)(i * 40));
		WORD wBobX = TO_PX(pVine->lAnchorX) - (VINE_W / 2);
		WORD wBobY = TO_PX(pVine->lAnchorY);
		if(wBobX < 0) {
			wBobX = 0;
		}
		if(wBobY < 0) {
			wBobY = 0;
		}
		bobInit(
			&pVine->sBob, VINE_W, VINE_H, 1,
			s_pFrame[4], s_pMask[4],
			(UWORD)wBobX, (UWORD)wBobY
		);
		vineAdvance(pVine, 1);
		pVine->lDeltaX = 0;
		pVine->lDeltaY = 0;
	}
}

void vinesReset(void) {
	s_ubGrabbed = GRAB_NONE;
	s_ubHalfFrame = 0;
	if(!s_uwCount) {
		return;
	}
	const tLevelVine *pDefs = levelGetVines();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		vineHome(&s_pVines[i], &pDefs[i], (UBYTE)(i * 40));
		vineAdvance(&s_pVines[i], 1);
		s_pVines[i].lDeltaX = 0;
		s_pVines[i].lDeltaY = 0;
	}
}

void vinesProcess(void) {
	if(VINES_EVERY_OTHER_FRAME) {
		s_ubHalfFrame ^= 1;
	}
	for(UWORD i = 0; i < s_uwCount; ++i) {
		UBYTE isHeld = (s_ubGrabbed == (UBYTE)i) ? 1 : 0;
		if(VINES_EVERY_OTHER_FRAME && !isHeld && s_ubHalfFrame) {
			s_pVines[i].lDeltaX = 0;
			s_pVines[i].lDeltaY = 0;
			continue;
		}
		vineAdvance(
			&s_pVines[i],
			(UBYTE)((VINES_EVERY_OTHER_FRAME && !isHeld) ? 2 : 1)
		);
	}
}

void vinesPushBobs(void) {
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
		tVine *pVine = &s_pVines[i];
		WORD wX = (WORD)pVine->sBob.sPos.uwX;
		WORD wY = (WORD)pVine->sBob.sPos.uwY;
		if(
			wX > wViewR || wY > wViewB ||
			(wX + VINE_W) < wViewL || (wY + VINE_H) < wViewT
		) {
			continue;
		}
		bobPush(&pVine->sBob);
	}
}

void vinesDestroy(void) {
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
	if(s_pMaskBm) {
		bitmapDestroy(s_pMaskBm);
		s_pMaskBm = 0;
	}
	s_uwCount = 0;
	s_ubGrabbed = GRAB_NONE;
}

UBYTE vinesTryGrab(WORD wLeft, WORD wTop, WORD wRight, WORD wBottom) {
	if(s_ubGrabbed != GRAB_NONE) {
		return 1;
	}
	for(UWORD i = 0; i < s_uwCount; ++i) {
		WORD wGx = TO_PX(s_pVines[i].lGrabX);
		WORD wGy = TO_PX(s_pVines[i].lGrabY);
		WORD wKnotL = wGx - GRAB_HALF;
		WORD wKnotR = wGx + GRAB_HALF - 1;
		WORD wKnotT = wGy - GRAB_HALF;
		WORD wKnotB = wGy + GRAB_HALF - 1;
		if(wKnotL <= wRight && wKnotR >= wLeft && wKnotT <= wBottom && wKnotB >= wTop) {
			s_ubGrabbed = (UBYTE)i;
			return 1;
		}
	}
	return 0;
}

void vinesGrabPos(LONG *plX, LONG *plY) {
	if(s_ubGrabbed >= s_uwCount) {
		return;
	}
	if(plX) {
		*plX = s_pVines[s_ubGrabbed].lGrabX;
	}
	if(plY) {
		*plY = s_pVines[s_ubGrabbed].lGrabY;
	}
}

void vinesGrabDelta(LONG *plDx, LONG *plDy) {
	if(s_ubGrabbed >= s_uwCount) {
		if(plDx) {
			*plDx = 0;
		}
		if(plDy) {
			*plDy = 0;
		}
		return;
	}
	if(plDx) {
		*plDx = s_pVines[s_ubGrabbed].lDeltaX;
	}
	if(plDy) {
		*plDy = s_pVines[s_ubGrabbed].lDeltaY;
	}
}

void vinesClearGrab(void) {
	s_ubGrabbed = GRAB_NONE;
}
