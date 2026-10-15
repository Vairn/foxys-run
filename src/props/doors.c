/* Channel-linked doors: vanish BOB, slide BOB, or off-screen terrain rewrite. */
#include "props/doors.h"
#include "player/player.h"
#include "audio/sfx.h"
#include "assets/assets.h"
#include "world/level.h"
#include "world/collision.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include <ace/managers/viewport/tilebuffer.h>
#include <ace/utils/bitmap.h>

#include <sprites/door.h>

#define DOOR_W DOOR_FRAME_W
#define DOOR_H DOOR_FRAME_H
#define DOOR_SPEED 256
#define DOOR_TERRAIN_MAX 64

typedef struct {
	tBob sBob;
	LONG lPosX;
	LONG lPosY;
	LONG lClosedX;
	LONG lClosedY;
	LONG lOpenX;
	LONG lOpenY;
	WORD wX;
	WORD wY;
	WORD wW;
	WORD wH;
	UWORD uwTileX0;
	UWORD uwTileY0;
	UWORD uwTileX1;
	UWORD uwTileY1;
	UWORD uwOrigTile[DOOR_TERRAIN_MAX];
	UBYTE ubOrigCol[DOOR_TERRAIN_MAX];
	UBYTE ubKind;
	UBYTE ubChannel;
	UBYTE isWantOpen;
	UBYTE isCommitted;
	UBYTE isBob;
	UBYTE ubCellCount;
} tDoor;

static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pBm, *s_pMaskBm;
static UBYTE *s_pFrame;
static UBYTE *s_pMask;
static tDoor s_pDoors[LEVEL_MAX_DOORS];
static UWORD s_uwCount;

static void doorSyncBob(tDoor *pDoor);
static UBYTE doorCanClose(const tDoor *pDoor);

static UBYTE rectOverlaps(
	WORD wAx0, WORD wAy0, WORD wAx1, WORD wAy1,
	WORD wBx0, WORD wBy0, WORD wBx1, WORD wBy1
) {
	return (wAx0 <= wBx1 && wAx1 >= wBx0 && wAy0 <= wBy1 && wAy1 >= wBy0) ? 1 : 0;
}

static UBYTE playerOverlapsWorld(WORD wX, WORD wY, WORD wW, WORD wH) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	return rectOverlaps(
		wX, wY, wX + wW - 1, wY + wH - 1,
		wPX, wPY, wPX + (WORD)uwPW - 1, wPY + (WORD)uwPH - 1
	);
}

static void doorSyncBob(tDoor *pDoor) {
	if(!pDoor->isBob) {
		return;
	}
	pDoor->wX = TO_PX(pDoor->lPosX);
	pDoor->wY = TO_PX(pDoor->lPosY);
	if(pDoor->wX < 0) {
		pDoor->wX = 0;
	}
	if(pDoor->wY < 0) {
		pDoor->wY = 0;
	}
	pDoor->sBob.sPos.uwX = (UWORD)pDoor->wX;
	pDoor->sBob.sPos.uwY = (UWORD)pDoor->wY;
}

static UBYTE doorIsBlocking(const tDoor *pDoor) {
	if(pDoor->ubKind == DOOR_KIND_TERRAIN) {
		return 0;
	}
	if(pDoor->ubKind == DOOR_KIND_VANISH) {
		if(pDoor->isWantOpen) {
			return 0;
		}
		if(!doorCanClose(pDoor)) {
			return 0;
		}
	}
	return 1;
}

static UBYTE doorRectOffBuffer(const tDoor *pDoor) {
	if(!s_pTileBuffer) {
		return 1;
	}
	for(UWORD x = pDoor->uwTileX0; x <= pDoor->uwTileX1; ++x) {
		for(UWORD y = pDoor->uwTileY0; y <= pDoor->uwTileY1; ++y) {
			if(tileBufferIsTileOnBuffer(s_pTileBuffer, x, y)) {
				return 0;
			}
		}
	}
	return 1;
}

static UWORD doorColIndex(UWORD uwX, UWORD uwY) {
	return (UWORD)(uwX * g_uwColTilesY + uwY);
}

static void doorWriteTerrain(tDoor *pDoor, UBYTE isOpen) {
	UWORD uwW = levelGetWidthTiles();
	UBYTE ubI = 0;
	for(UWORD x = pDoor->uwTileX0; x <= pDoor->uwTileX1; ++x) {
		for(UWORD y = pDoor->uwTileY0; y <= pDoor->uwTileY1; ++y) {
			if(ubI >= pDoor->ubCellCount) {
				return;
			}
			if(x >= uwW || y >= levelGetHeightTiles()) {
				++ubI;
				continue;
			}
			if(isOpen) {
				s_pTileBuffer->pTileData[x][y] = 0;
				g_ubColMap[doorColIndex(x, y)] = COL_EMPTY;
			}
			else {
				s_pTileBuffer->pTileData[x][y] = pDoor->uwOrigTile[ubI];
				g_ubColMap[doorColIndex(x, y)] = pDoor->ubOrigCol[ubI];
			}
			++ubI;
		}
	}
}

static void doorSaveTerrain(tDoor *pDoor) {
	UWORD uwW = levelGetWidthTiles();
	UBYTE ubI = 0;
	pDoor->ubCellCount = 0;
	for(UWORD x = pDoor->uwTileX0; x <= pDoor->uwTileX1; ++x) {
		for(UWORD y = pDoor->uwTileY0; y <= pDoor->uwTileY1; ++y) {
			if(ubI >= DOOR_TERRAIN_MAX) {
				pDoor->ubCellCount = ubI;
				return;
			}
			if(x < uwW && y < levelGetHeightTiles() && s_pTileBuffer) {
				pDoor->uwOrigTile[ubI] = s_pTileBuffer->pTileData[x][y];
				pDoor->ubOrigCol[ubI] = g_ubColMap[doorColIndex(x, y)];
			}
			else {
				pDoor->uwOrigTile[ubI] = 0;
				pDoor->ubOrigCol[ubI] = COL_EMPTY;
			}
			++ubI;
		}
	}
	pDoor->ubCellCount = ubI;
}

static UBYTE doorCanClose(const tDoor *pDoor) {
	WORD wX = TO_PX(pDoor->lClosedX);
	WORD wY = TO_PX(pDoor->lClosedY);
	return playerOverlapsWorld(wX, wY, pDoor->wW, pDoor->wH) ? 0 : 1;
}

static UBYTE doorTerrainPlayerClear(const tDoor *pDoor) {
	WORD wX = (WORD)(pDoor->uwTileX0 << TILE_SHIFT);
	WORD wY = (WORD)(pDoor->uwTileY0 << TILE_SHIFT);
	WORD wW = (WORD)((pDoor->uwTileX1 - pDoor->uwTileX0 + 1) << TILE_SHIFT);
	WORD wH = (WORD)((pDoor->uwTileY1 - pDoor->uwTileY0 + 1) << TILE_SHIFT);
	return playerOverlapsWorld(wX, wY, wW, wH) ? 0 : 1;
}

void doorsLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_pBm = 0;
	s_pMaskBm = 0;
	s_pFrame = 0;
	s_pMask = 0;
	s_uwCount = 0;

	if(!levelGetDoorCount()) {
		return;
	}
	s_pBm = sunnyLoadBitmap("data/sprites/door.bm", 0);
	s_pMaskBm = sunnyLoadBitmap("data/sprites/door_mask.bm", 0);
	if(s_pBm) {
		s_pFrame = bobCalcFrameAddress(s_pBm, 0);
	}
	if(s_pMaskBm) {
		s_pMask = bobCalcFrameAddress(s_pMaskBm, 0);
	}
}

void doorsInitBobs(void) {
	s_uwCount = levelGetDoorCount();
	if(s_uwCount > LEVEL_MAX_DOORS) {
		s_uwCount = LEVEL_MAX_DOORS;
	}
	if(!s_uwCount) {
		return;
	}

	const tLevelDoor *pDefs = levelGetDoors();
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		const tLevelDoor *pDef = &pDefs[i];
		UWORD uwX0 = pDef->uwTileX;
		UWORD uwY0 = pDef->uwTileY;
		UWORD uwX1 = pDef->uwEndX;
		UWORD uwY1 = pDef->uwEndY;
		if(uwX1 < uwX0) {
			UWORD uwT = uwX0;
			uwX0 = uwX1;
			uwX1 = uwT;
		}
		if(uwY1 < uwY0) {
			UWORD uwT = uwY0;
			uwY0 = uwY1;
			uwY1 = uwT;
		}

		pDoor->ubKind = (UBYTE)pDef->uwKind;
		pDoor->ubChannel = (UBYTE)pDef->uwChannel;
		pDoor->isWantOpen = 0;
		pDoor->isCommitted = 0;
		pDoor->uwTileX0 = pDef->uwTileX;
		pDoor->uwTileY0 = pDef->uwTileY;
		pDoor->uwTileX1 = pDef->uwEndX;
		pDoor->uwTileY1 = pDef->uwEndY;

		if(pDoor->ubKind == DOOR_KIND_TERRAIN) {
			pDoor->uwTileX0 = uwX0;
			pDoor->uwTileY0 = uwY0;
			pDoor->uwTileX1 = uwX1;
			pDoor->uwTileY1 = uwY1;
			pDoor->isBob = 0;
			pDoor->wW = (WORD)((uwX1 - uwX0 + 1) << TILE_SHIFT);
			pDoor->wH = (WORD)((uwY1 - uwY0 + 1) << TILE_SHIFT);
			pDoor->lClosedX = PX(uwX0 << TILE_SHIFT);
			pDoor->lClosedY = PX(uwY0 << TILE_SHIFT);
			pDoor->lOpenX = pDoor->lClosedX;
			pDoor->lOpenY = pDoor->lClosedY;
			pDoor->lPosX = pDoor->lClosedX;
			pDoor->lPosY = pDoor->lClosedY;
			doorSaveTerrain(pDoor);
			continue;
		}

		pDoor->isBob = 1;
		pDoor->wW = DOOR_W;
		pDoor->wH = DOOR_H;
		pDoor->uwTileX0 = pDef->uwTileX;
		pDoor->uwTileY0 = pDef->uwTileY;
		pDoor->uwTileX1 = pDef->uwTileX;
		pDoor->uwTileY1 = (UWORD)(pDef->uwTileY + 1);
		pDoor->lClosedX = PX(pDef->uwTileX << TILE_SHIFT);
		pDoor->lClosedY = PX(pDef->uwTileY << TILE_SHIFT);
		if(pDoor->ubKind == DOOR_KIND_SLIDE) {
			pDoor->lOpenX = PX(pDef->uwEndX << TILE_SHIFT);
			pDoor->lOpenY = PX(pDef->uwEndY << TILE_SHIFT);
		}
		else {
			pDoor->lOpenX = pDoor->lClosedX;
			pDoor->lOpenY = pDoor->lClosedY;
		}
		pDoor->lPosX = pDoor->lClosedX;
		pDoor->lPosY = pDoor->lClosedY;
		doorSyncBob(pDoor);
		if(!s_pFrame || !s_pMask) {
			pDoor->isBob = 0;
			continue;
		}
		bobInit(
			&pDoor->sBob, DOOR_W, DOOR_H, 1,
			s_pFrame, s_pMask,
			(UWORD)pDoor->wX, (UWORD)pDoor->wY
		);
	}
}

void doorsReset(void) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		pDoor->isWantOpen = 0;
		pDoor->lPosX = pDoor->lClosedX;
		pDoor->lPosY = pDoor->lClosedY;
		doorSyncBob(pDoor);
		if(pDoor->ubKind == DOOR_KIND_TERRAIN && pDoor->isCommitted) {
			doorWriteTerrain(pDoor, 0);
		}
		pDoor->isCommitted = 0;
	}
}

static void doorApproach(tDoor *pDoor, LONG lTx, LONG lTy) {
	LONG lDx = lTx - pDoor->lPosX;
	LONG lDy = lTy - pDoor->lPosY;
	if(lDx > DOOR_SPEED) {
		lDx = DOOR_SPEED;
	}
	else if(lDx < -DOOR_SPEED) {
		lDx = -DOOR_SPEED;
	}
	if(lDy > DOOR_SPEED) {
		lDy = DOOR_SPEED;
	}
	else if(lDy < -DOOR_SPEED) {
		lDy = -DOOR_SPEED;
	}
	pDoor->lPosX += lDx;
	pDoor->lPosY += lDy;
}

void doorsProcess(void) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(pDoor->ubKind == DOOR_KIND_TERRAIN) {
			if(pDoor->isWantOpen && !pDoor->isCommitted && doorRectOffBuffer(pDoor)) {
				doorWriteTerrain(pDoor, 1);
				pDoor->isCommitted = 1;
			}
			else if(
				!pDoor->isWantOpen && pDoor->isCommitted &&
				doorRectOffBuffer(pDoor) && doorTerrainPlayerClear(pDoor)
			) {
				doorWriteTerrain(pDoor, 0);
				pDoor->isCommitted = 0;
			}
			continue;
		}
		if(pDoor->ubKind == DOOR_KIND_VANISH) {
			continue;
		}
		if(!pDoor->isWantOpen && !doorCanClose(pDoor)) {
			continue;
		}
		LONG lTx = pDoor->isWantOpen ? pDoor->lOpenX : pDoor->lClosedX;
		LONG lTy = pDoor->isWantOpen ? pDoor->lOpenY : pDoor->lClosedY;
		doorApproach(pDoor, lTx, lTy);
		doorSyncBob(pDoor);
	}
}

void doorsPushBobs(void) {
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
		tDoor *pDoor = &s_pDoors[i];
		if(!pDoor->isBob || !doorIsBlocking(pDoor)) {
			continue;
		}
		WORD wX = pDoor->wX;
		WORD wY = pDoor->wY;
		if(
			wX > wViewR || wY > wViewB ||
			(wX + pDoor->wW) < wViewL || (wY + pDoor->wH) < wViewT
		) {
			continue;
		}
		bobPush(&pDoor->sBob);
	}
}

void doorsDestroy(void) {
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
	if(s_pMaskBm) {
		bitmapDestroy(s_pMaskBm);
		s_pMaskBm = 0;
	}
	s_pFrame = 0;
	s_pMask = 0;
	s_uwCount = 0;
}

void doorsOpenChannel(UBYTE ubChannel) {
	UBYTE isAny = 0;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		if(s_pDoors[i].ubChannel == ubChannel && !s_pDoors[i].isWantOpen) {
			s_pDoors[i].isWantOpen = 1;
			isAny = 1;
		}
	}
	if(isAny) {
		sfxPlayDoor();
	}
}

void doorsCloseChannel(UBYTE ubChannel) {
	UBYTE isAny = 0;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		if(s_pDoors[i].ubChannel == ubChannel && s_pDoors[i].isWantOpen) {
			s_pDoors[i].isWantOpen = 0;
			isAny = 1;
		}
	}
	if(isAny) {
		sfxPlayDoor();
	}
}

static UBYTE doorHitProbe(const tDoor *pDoor, WORD wX, WORD wY0, WORD wY1) {
	if(!doorIsBlocking(pDoor)) {
		return 0;
	}
	WORD wL = pDoor->wX;
	WORD wR = pDoor->wX + pDoor->wW - 1;
	WORD wT = pDoor->wY;
	WORD wB = pDoor->wY + pDoor->wH - 1;
	return (wX >= wL && wX <= wR && wY0 <= wB && wY1 >= wT) ? 1 : 0;
}

UBYTE doorsBlockedVSpan(WORD wX, WORD wY0, WORD wY1) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		if(doorHitProbe(&s_pDoors[i], wX, wY0, wY1)) {
			return 1;
		}
	}
	return 0;
}

WORD doorsSnapX(WORD wX, WORD wY0, WORD wY1, UBYTE isMovingRight) {
	WORD wBest = -1;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorHitProbe(pDoor, wX, wY0, wY1)) {
			continue;
		}
		WORD wEdge = isMovingRight ? (pDoor->wX - 1) : (pDoor->wX + pDoor->wW);
		if(wBest < 0) {
			wBest = wEdge;
		}
		else if(isMovingRight && wEdge < wBest) {
			wBest = wEdge;
		}
		else if(!isMovingRight && wEdge > wBest) {
			wBest = wEdge;
		}
	}
	return wBest;
}

UBYTE doorsBlockedCeiling(WORD wY, WORD wX0, WORD wX1) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorIsBlocking(pDoor)) {
			continue;
		}
		WORD wL = pDoor->wX;
		WORD wR = pDoor->wX + pDoor->wW - 1;
		WORD wT = pDoor->wY;
		WORD wB = pDoor->wY + pDoor->wH - 1;
		if(wY >= wT && wY <= wB && wX0 <= wR && wX1 >= wL) {
			return 1;
		}
	}
	return 0;
}

WORD doorsSnapCeiling(WORD wY, WORD wX0, WORD wX1) {
	WORD wBest = -1;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorIsBlocking(pDoor)) {
			continue;
		}
		WORD wL = pDoor->wX;
		WORD wR = pDoor->wX + pDoor->wW - 1;
		WORD wT = pDoor->wY;
		WORD wB = pDoor->wY + pDoor->wH - 1;
		if(wY >= wT && wY <= wB && wX0 <= wR && wX1 >= wL) {
			WORD wSnap = wB + 1;
			if(wBest < 0 || wSnap > wBest) {
				wBest = wSnap;
			}
		}
	}
	return wBest;
}

WORD doorsTryLand(WORD wOldBottom, WORD wNewBottom, WORD wLeft, WORD wRight) {
	WORD wBest = -1;
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorIsBlocking(pDoor)) {
			continue;
		}
		WORD wTop = pDoor->wY;
		WORD wL = pDoor->wX;
		WORD wR = pDoor->wX + pDoor->wW - 1;
		if(wNewBottom + 1 < wTop) {
			continue;
		}
		if(wOldBottom >= wTop + 2) {
			continue;
		}
		if(wLeft > wR || wRight < wL) {
			continue;
		}
		if(wBest < 0 || wTop < wBest) {
			wBest = wTop;
		}
	}
	return wBest;
}

UBYTE doorsProbeGround(WORD wBelow, WORD wLeft, WORD wRight) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorIsBlocking(pDoor)) {
			continue;
		}
		WORD wTop = pDoor->wY;
		WORD wDiff = (WORD)(wBelow - wTop);
		if(wDiff < (WORD)-3 || wDiff > 4) {
			continue;
		}
		WORD wL = pDoor->wX;
		WORD wR = pDoor->wX + pDoor->wW - 1;
		if(wLeft > wR || wRight < wL) {
			continue;
		}
		return 1;
	}
	return 0;
}

UBYTE doorsHitWorldRect(WORD wX0, WORD wY0, WORD wX1, WORD wY1) {
	for(UWORD i = 0; i < s_uwCount; ++i) {
		tDoor *pDoor = &s_pDoors[i];
		if(!doorIsBlocking(pDoor)) {
			continue;
		}
		if(rectOverlaps(
			pDoor->wX, pDoor->wY,
			pDoor->wX + pDoor->wW - 1, pDoor->wY + pDoor->wH - 1,
			wX0, wY0, wX1, wY1
		)) {
			return 1;
		}
	}
	return 0;
}
