/* Channel-linked switches — 16x16 BOBs, shoot or touch, one-shot or toggle. */
#include "switches.h"
#include "doors.h"
#include "platforms.h"
#include "player.h"
#include "sfx.h"
#include "assets.h"
#include "level.h"
#include "game_defs.h"

#include <ace/managers/bob.h>
#include <ace/utils/bitmap.h>

#include <sprites/switch.h>

#define SWITCH_W SWITCH_FRAME_W
#define SWITCH_H SWITCH_FRAME_H
#define SWITCH_FR_LEVER_OFF 0
#define SWITCH_FR_LEVER_ON  1
#define SWITCH_FR_PLATE_OFF 2
#define SWITCH_FR_PLATE_ON  3

typedef struct {
	tBob sBob;
	WORD wX;
	WORD wY;
	UBYTE ubChannel;
	UBYTE ubFlags;
	UBYTE isTouching;
} tSwitch;

static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pBm, *s_pMaskBm;
static UBYTE *s_pFrame[SWITCH_FRAME_COUNT];
static UBYTE *s_pMask[SWITCH_FRAME_COUNT];
static tSwitch s_pSwitches[LEVEL_MAX_SWITCHES];
static UWORD s_uwCount;
static UBYTE s_ubChannelOn[SWITCH_CHANNEL_MAX + 1];

static UBYTE switchIsTouch(const tSwitch *pSw) {
	return (pSw->ubFlags & SWITCH_FLAG_TOUCH) ? 1 : 0;
}

static UBYTE switchIsToggle(const tSwitch *pSw) {
	return (pSw->ubFlags & SWITCH_FLAG_TOGGLE) ? 1 : 0;
}

static void switchSetFrame(tSwitch *pSw) {
	UBYTE ubOn = 0;
	UBYTE ubFr;
	if(pSw->ubChannel <= SWITCH_CHANNEL_MAX) {
		ubOn = s_ubChannelOn[pSw->ubChannel];
	}
	if(switchIsTouch(pSw)) {
		ubFr = ubOn ? SWITCH_FR_PLATE_ON : SWITCH_FR_PLATE_OFF;
	}
	else {
		ubFr = ubOn ? SWITCH_FR_LEVER_ON : SWITCH_FR_LEVER_OFF;
	}
	if(ubFr >= SWITCH_FRAME_COUNT) {
		ubFr = 0;
	}
	if(s_pFrame[ubFr] && s_pMask[ubFr]) {
		bobSetFrame(&pSw->sBob, s_pFrame[ubFr], s_pMask[ubFr]);
	}
}

static void switchRefreshFrames(void) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		switchSetFrame(&s_pSwitches[i]);
	}
}

static UBYTE switchOverlaps(
	const tSwitch *pSw, WORD wX0, WORD wY0, WORD wX1, WORD wY1
) {
	WORD wR = pSw->wX + SWITCH_W - 1;
	WORD wB = pSw->wY + SWITCH_H - 1;
	return (pSw->wX <= wX1 && wR >= wX0 && pSw->wY <= wY1 && wB >= wY0) ? 1 : 0;
}

static void switchFire(tSwitch *pSw) {
	UBYTE ubCh = pSw->ubChannel;
	if(ubCh < 1 || ubCh > SWITCH_CHANNEL_MAX) {
		return;
	}
	if(switchIsToggle(pSw)) {
		if(s_ubChannelOn[ubCh]) {
			switchesDeactivateChannel(ubCh);
			sfxPlaySwitchOff();
		}
		else {
			switchesActivateChannel(ubCh);
			sfxPlaySwitchOn();
		}
		return;
	}
	if(s_ubChannelOn[ubCh]) {
		return;
	}
	switchesActivateChannel(ubCh);
	sfxPlaySwitchOn();
}

void switchesLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_pBm = 0;
	s_pMaskBm = 0;
	s_uwCount = 0;
	for(UBYTE i = 0; i < SWITCH_FRAME_COUNT; ++i) {
		s_pFrame[i] = 0;
		s_pMask[i] = 0;
	}
	for(UBYTE i = 0; i <= SWITCH_CHANNEL_MAX; ++i) {
		s_ubChannelOn[i] = 0;
	}

	if(!levelGetSwitchCount()) {
		return;
	}
	s_pBm = sunnyLoadBitmap("data/sprites/switch.bm", 0);
	s_pMaskBm = sunnyLoadBitmap("data/sprites/switch_mask.bm", 0);
	if(!s_pBm || !s_pMaskBm) {
		return;
	}
	for(UBYTE i = 0; i < SWITCH_FRAME_COUNT; ++i) {
		s_pFrame[i] = bobCalcFrameAddress(s_pBm, i * SWITCH_H);
		s_pMask[i] = bobCalcFrameAddress(s_pMaskBm, i * SWITCH_H);
	}
}

void switchesInitBobs(void) {
	s_uwCount = levelGetSwitchCount();
	if(s_uwCount > LEVEL_MAX_SWITCHES) {
		s_uwCount = LEVEL_MAX_SWITCHES;
	}
	if(!s_uwCount || !s_pFrame[0] || !s_pMask[0]) {
		s_uwCount = 0;
		return;
	}

	const tLevelSwitch *pDefs = levelGetSwitches();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tSwitch *pSw = &s_pSwitches[i];
		pSw->wX = (WORD)(pDefs[i].uwTileX << TILE_SHIFT);
		pSw->wY = (WORD)(pDefs[i].uwTileY << TILE_SHIFT);
		pSw->ubChannel = (UBYTE)pDefs[i].uwChannel;
		pSw->ubFlags = (UBYTE)pDefs[i].uwFlags;
		pSw->isTouching = 0;
		if(pSw->wX < 0) {
			pSw->wX = 0;
		}
		if(pSw->wY < 0) {
			pSw->wY = 0;
		}
		bobInit(
			&pSw->sBob, SWITCH_W, SWITCH_H, 1,
			s_pFrame[0], s_pMask[0],
			(UWORD)pSw->wX, (UWORD)pSw->wY
		);
		switchSetFrame(pSw);
	}
}

void switchesReset(void) {
	for(UBYTE i = 0; i <= SWITCH_CHANNEL_MAX; ++i) {
		s_ubChannelOn[i] = 0;
	}
	for(UWORD i = 0; i < s_uwCount; ++i) {
		s_pSwitches[i].isTouching = 0;
		switchSetFrame(&s_pSwitches[i]);
	}
}

void switchesProcess(void) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;

	for(UWORD i = 0; i < s_uwCount; ++i) {
		tSwitch *pSw = &s_pSwitches[i];
		if(!switchIsTouch(pSw)) {
			pSw->isTouching = 0;
			continue;
		}
		UBYTE isHit = switchOverlaps(pSw, wPX, wPY, wPR, wPB);
		if(isHit && !pSw->isTouching) {
			switchFire(pSw);
		}
		pSw->isTouching = isHit;
	}
}

void switchesPushBobs(void) {
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
		tSwitch *pSw = &s_pSwitches[i];
		if(
			pSw->wX > wViewR || pSw->wY > wViewB ||
			(pSw->wX + SWITCH_W) < wViewL || (pSw->wY + SWITCH_H) < wViewT
		) {
			continue;
		}
		bobPush(&pSw->sBob);
	}
}

void switchesDestroy(void) {
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

UBYTE switchesHitWorldRect(WORD wX0, WORD wY0, WORD wX1, WORD wY1) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tSwitch *pSw = &s_pSwitches[i];
		if(switchIsTouch(pSw)) {
			continue;
		}
		if(switchOverlaps(pSw, wX0, wY0, wX1, wY1)) {
			switchFire(pSw);
			return 1;
		}
	}
	return 0;
}

void switchesActivateChannel(UBYTE ubChannel) {
	if(ubChannel < 1 || ubChannel > SWITCH_CHANNEL_MAX) {
		return;
	}
	if(s_ubChannelOn[ubChannel]) {
		return;
	}
	s_ubChannelOn[ubChannel] = 1;
	switchRefreshFrames();
	doorsOpenChannel(ubChannel);
	platformsActivateChannel(ubChannel);
}

void switchesDeactivateChannel(UBYTE ubChannel) {
	if(ubChannel < 1 || ubChannel > SWITCH_CHANNEL_MAX) {
		return;
	}
	if(!s_ubChannelOn[ubChannel]) {
		return;
	}
	s_ubChannelOn[ubChannel] = 0;
	switchRefreshFrames();
	doorsCloseChannel(ubChannel);
	platformsDeactivateChannel(ubChannel);
}
