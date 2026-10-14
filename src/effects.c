/* Death poof and pickup sparkle BOB pools. */
#include "effects.h"
#include "assets.h"
#include "level.h"
#include "game_defs.h"

#include <ace/managers/bob.h>
#include <ace/utils/bitmap.h>

#include <sprites/enemy_death.h>
#include <sprites/item_feedback.h>

#define DEATH_FX_MAX 2       // simultaneous enemy death poofs
#define PICKUP_FX_MAX 2      // simultaneous pickup sparkles
#define DEATH_FX_DELAY 4     // ticks per frame (~12fps)
#define PICKUP_FX_DELAY 4

typedef struct {
	tBob sBob;
	UBYTE isActive;
	UBYTE ubFrame;
	UBYTE ubTimer;
} tEffect;

static tTileBufferManager *s_pTileBuffer;

static tBitMap *s_pDeath, *s_pDeathMask;
static tBitMap *s_pFeedback, *s_pFeedbackMask;

static UBYTE *s_pDeathFrame[ENEMY_DEATH_FRAME_COUNT];
static UBYTE *s_pDeathMaskAddr[ENEMY_DEATH_FRAME_COUNT];
static UBYTE *s_pFeedbackFrame[ITEM_FEEDBACK_FRAME_COUNT];
static UBYTE *s_pFeedbackMaskAddr[ITEM_FEEDBACK_FRAME_COUNT];

static tEffect s_pDeathFx[DEATH_FX_MAX];
static tEffect s_pPickupFx[PICKUP_FX_MAX];

//------------------------------------------------------------------- HELPERS

static void fxInitPool(
	tEffect *pPool, UBYTE ubMax, UWORD uwW, UWORD uwH,
	UBYTE *pFrame0, UBYTE *pMask0
) {
	for(UBYTE i = 0; i < ubMax; ++i) {
		bobInit(&pPool[i].sBob, uwW, uwH, 1, pFrame0, pMask0, 0, 0);
		pPool[i].isActive = 0;
		pPool[i].ubFrame = 0;
		pPool[i].ubTimer = 0;
	}
}

static void fxSpawn(
	tEffect *pPool, UBYTE ubMax, UBYTE **pFrames, UBYTE **pMasks,
	WORD wCenterX, WORD wCenterY, UWORD uwW, UWORD uwH
) {
	for(UBYTE i = 0; i < ubMax; ++i) {
		tEffect *pFx = &pPool[i];
		if(pFx->isActive) {
			continue;
		}
		WORD wX = wCenterX - (WORD)(uwW / 2);
		WORD wY = wCenterY - (WORD)(uwH / 2);
		if(wX < 0) { wX = 0; }
		if(wY < 0) { wY = 0; }
		pFx->sBob.sPos.uwX = (UWORD)wX;
		pFx->sBob.sPos.uwY = (UWORD)wY;
		bobSetFrame(&pFx->sBob, pFrames[0], pMasks[0]);
		pFx->ubFrame = 0;
		pFx->ubTimer = 0;
		pFx->isActive = 1;
		return; // spawned into the first free slot
	}
	// No free slot: silently drop (effects are non-essential polish).
}

static void fxProcessPool(
	tEffect *pPool, UBYTE ubMax, UBYTE ubCount, UBYTE ubDelay,
	UBYTE **pFrames, UBYTE **pMasks
) {
	for(UBYTE i = 0; i < ubMax; ++i) {
		tEffect *pFx = &pPool[i];
		if(!pFx->isActive) {
			continue;
		}
		if(++pFx->ubTimer >= ubDelay) {
			pFx->ubTimer = 0;
			if(++pFx->ubFrame >= ubCount) {
				pFx->isActive = 0; // clip finished — retire the slot
				continue;
			}
			bobSetFrame(&pFx->sBob, pFrames[pFx->ubFrame], pMasks[pFx->ubFrame]);
		}
	}
}

static void fxPushPool(
	tEffect *pPool, UBYTE ubMax,
	WORD wViewL, WORD wViewT, WORD wViewR, WORD wViewB
) {
	for(UBYTE i = 0; i < ubMax; ++i) {
		tEffect *pFx = &pPool[i];
		if(!pFx->isActive) {
			continue;
		}
		WORD wX = (WORD)pFx->sBob.sPos.uwX;
		WORD wY = (WORD)pFx->sBob.sPos.uwY;
		if(wX > wViewR || wY > wViewB
			|| (wX + (WORD)pFx->sBob.uwWidth) < wViewL
			|| (wY + (WORD)pFx->sBob.uwHeight) < wViewT) {
			continue; // off-screen — skip draw (auto-undrawn by the manager)
		}
		bobPush(&pFx->sBob);
	}
}

//------------------------------------------------------------------- PUBLIC

void effectsLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;

	if(levelIsWinter()) {
		s_pDeath = sunnyLoadBitmap("data/sprites/enemy_death_w.bm", 0);
		s_pDeathMask = sunnyLoadBitmap("data/sprites/enemy_death_w_mask.bm", 0);
		s_pFeedback = sunnyLoadBitmap("data/sprites/item_feedback_w.bm", 0);
		s_pFeedbackMask = sunnyLoadBitmap("data/sprites/item_feedback_w_mask.bm", 0);
	}
	else {
		char szDeath[LEVEL_PATH_MAX];
		char szDeathMask[LEVEL_PATH_MAX];
		char szFb[LEVEL_PATH_MAX];
		char szFbMask[LEVEL_PATH_MAX];
		levelSpritePath(szDeath, "enemy_death.bm");
		levelSpritePath(szDeathMask, "enemy_death_mask.bm");
		levelSpritePath(szFb, "item_feedback.bm");
		levelSpritePath(szFbMask, "item_feedback_mask.bm");
		s_pDeath = sunnyLoadBitmap(szDeath, 0);
		s_pDeathMask = sunnyLoadBitmap(szDeathMask, 0);
		s_pFeedback = sunnyLoadBitmap(szFb, 0);
		s_pFeedbackMask = sunnyLoadBitmap(szFbMask, 0);
	}

	for(UBYTE i = 0; i < ENEMY_DEATH_FRAME_COUNT; ++i) {
		s_pDeathFrame[i] = bobCalcFrameAddress(s_pDeath, i * ENEMY_DEATH_FRAME_H);
		s_pDeathMaskAddr[i] = bobCalcFrameAddress(s_pDeathMask, i * ENEMY_DEATH_FRAME_H);
	}
	for(UBYTE i = 0; i < ITEM_FEEDBACK_FRAME_COUNT; ++i) {
		s_pFeedbackFrame[i] = bobCalcFrameAddress(s_pFeedback, i * ITEM_FEEDBACK_FRAME_H);
		s_pFeedbackMaskAddr[i] = bobCalcFrameAddress(s_pFeedbackMask, i * ITEM_FEEDBACK_FRAME_H);
	}
}

void effectsInitBobs(void) {
	if(s_pDeathFrame[0] && s_pDeathMaskAddr[0]) {
		fxInitPool(
			s_pDeathFx, DEATH_FX_MAX, ENEMY_DEATH_FRAME_W, ENEMY_DEATH_FRAME_H,
			s_pDeathFrame[0], s_pDeathMaskAddr[0]
		);
	}
	if(s_pFeedbackFrame[0] && s_pFeedbackMaskAddr[0]) {
		fxInitPool(
			s_pPickupFx, PICKUP_FX_MAX, ITEM_FEEDBACK_FRAME_W, ITEM_FEEDBACK_FRAME_H,
			s_pFeedbackFrame[0], s_pFeedbackMaskAddr[0]
		);
	}
}

void effectsReset(void) {
	for(UBYTE i = 0; i < DEATH_FX_MAX; ++i) {
		s_pDeathFx[i].isActive = 0;
	}
	for(UBYTE i = 0; i < PICKUP_FX_MAX; ++i) {
		s_pPickupFx[i].isActive = 0;
	}
}

void effectsProcess(void) {
	fxProcessPool(
		s_pDeathFx, DEATH_FX_MAX, ENEMY_DEATH_FRAME_COUNT, DEATH_FX_DELAY,
		s_pDeathFrame, s_pDeathMaskAddr
	);
	fxProcessPool(
		s_pPickupFx, PICKUP_FX_MAX, ITEM_FEEDBACK_FRAME_COUNT, PICKUP_FX_DELAY,
		s_pFeedbackFrame, s_pFeedbackMaskAddr
	);
}

void effectsPushBobs(void) {
	UWORD uwCamX = s_pTileBuffer->pCamera->uPos.uwX;
	UWORD uwCamY = s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewL = (WORD)uwCamX - CULL_MARGIN;
	WORD wViewT = (WORD)uwCamY - CULL_MARGIN;
	WORD wViewR = (WORD)uwCamX + SCREEN_W + CULL_MARGIN;
	WORD wViewB = (WORD)uwCamY + SCREEN_H + CULL_MARGIN;

	fxPushPool(s_pDeathFx, DEATH_FX_MAX, wViewL, wViewT, wViewR, wViewB);
	fxPushPool(s_pPickupFx, PICKUP_FX_MAX, wViewL, wViewT, wViewR, wViewB);
}

void effectsDestroy(void) {
	bitmapDestroy(s_pDeath);
	bitmapDestroy(s_pDeathMask);
	bitmapDestroy(s_pFeedback);
	bitmapDestroy(s_pFeedbackMask);
}

void effectsSpawnDeath(WORD wCenterX, WORD wCenterY) {
	fxSpawn(
		s_pDeathFx, DEATH_FX_MAX, s_pDeathFrame, s_pDeathMaskAddr,
		wCenterX, wCenterY, ENEMY_DEATH_FRAME_W, ENEMY_DEATH_FRAME_H
	);
}

void effectsSpawnPickup(WORD wCenterX, WORD wCenterY) {
	fxSpawn(
		s_pPickupFx, PICKUP_FX_MAX, s_pFeedbackFrame, s_pFeedbackMaskAddr,
		wCenterX, wCenterY, ITEM_FEEDBACK_FRAME_W, ITEM_FEEDBACK_FRAME_H
	);
}
