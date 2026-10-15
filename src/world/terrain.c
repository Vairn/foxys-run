/* Crumble / single-use walk-tops — mutate pTileData + g_ubColMap, restamp in draw. */
#include "world/terrain.h"
#include "world/collision.h"
#include "player/player.h"
#include "world/level.h"
#include "game/game_defs.h"

#include <tiles/tilecollision.h>
#include "debug/serial_log.h"

#define TERRAIN_MAX_CRUMBLE 32
#define TERRAIN_MAX_ONCE 16
#define TERRAIN_MAX_DIRTY 48

#define CRUMBLE_ARM_FRAMES 40
#define CRUMBLE_SHAKE_FRAMES 20
#define CRUMBLE_RESPAWN_FRAMES 150

#define CRUMBLE_IDLE 0
#define CRUMBLE_ARM 1
#define CRUMBLE_SHAKE 2
#define CRUMBLE_GONE 3

#define ONCE_IDLE 0
#define ONCE_RIDING 1
#define ONCE_GONE 2

typedef struct {
	UWORD uwX;
	UWORD uwY;
	UWORD uwOrig;
	UBYTE ubState;
	UBYTE ubTimer;
} tCrumble;

typedef struct {
	UWORD uwX;
	UWORD uwY;
	UWORD uwOrig;
	UBYTE ubState;
} tOnce;

static tTileBufferManager *s_pTileBuffer;
static tCrumble s_pCrumbles[TERRAIN_MAX_CRUMBLE];
static tOnce s_pOnce[TERRAIN_MAX_ONCE];
static UWORD s_uwCrumbleCount;
static UWORD s_uwOnceCount;
static UWORD s_pDirtyX[TERRAIN_MAX_DIRTY];
static UWORD s_pDirtyY[TERRAIN_MAX_DIRTY];
static UBYTE s_ubDirtyCount;

static UWORD colIndex(UWORD uwX, UWORD uwY) {
	return (UWORD)(uwX * g_uwColTilesY + uwY);
}

static void markDirty(UWORD uwX, UWORD uwY) {
	if(s_ubDirtyCount >= TERRAIN_MAX_DIRTY) {
		return;
	}
	for(UBYTE i = 0; i < s_ubDirtyCount; ++i) {
		if(s_pDirtyX[i] == uwX && s_pDirtyY[i] == uwY) {
			return;
		}
	}
	s_pDirtyX[s_ubDirtyCount] = uwX;
	s_pDirtyY[s_ubDirtyCount] = uwY;
	++s_ubDirtyCount;
}

static void writeCell(UWORD uwX, UWORD uwY, UWORD uwTile, UBYTE ubCol) {
	if(!s_pTileBuffer) {
		return;
	}
	s_pTileBuffer->pTileData[uwX][uwY] = uwTile;
	if(g_ubColMap) {
		g_ubColMap[colIndex(uwX, uwY)] = ubCol;
	}
	markDirty(uwX, uwY);
}

static UWORD crumbleCrackTile(UWORD uwOrig) {
	if(uwOrig >= TILE_CRUMBLE_L && uwOrig <= TILE_CRUMBLE_R) {
		return (UWORD)(uwOrig + 3);
	}
	return uwOrig;
}

static UBYTE feetOnCell(WORD wLeft, WORD wRight, WORD wBelow, UWORD uwX, UWORD uwY) {
	WORD wTop = (WORD)(uwY << TILE_SHIFT);
	WORD wCellL = (WORD)(uwX << TILE_SHIFT);
	WORD wCellR = wCellL + TILE_SIZE - 1;
	if(wBelow < wTop || wBelow > wTop + 3) {
		return 0;
	}
	return (wCellL <= wRight && wCellR >= wLeft) ? 1 : 0;
}

void terrainInit(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_uwCrumbleCount = 0;
	s_uwOnceCount = 0;
	s_ubDirtyCount = 0;
	if(!g_ubColMap) {
		return;
	}
	UWORD uwW = levelGetWidthTiles();
	if(uwW > LEVEL_MAX_WIDTH_TILES) {
		uwW = LEVEL_MAX_WIDTH_TILES;
	}
	for(UWORD x = 0; x < uwW; ++x) {
		UWORD uwCol = colIndex(x, 0);
		for(UWORD y = 0; y < levelGetHeightTiles(); ++y) {
			UBYTE ubCol = g_ubColMap[uwCol + y];
			UWORD uwTile = pTileBuffer->pTileData[x][y];
			if(ubCol == COL_CRUMBLE && s_uwCrumbleCount < TERRAIN_MAX_CRUMBLE) {
				tCrumble *pC = &s_pCrumbles[s_uwCrumbleCount++];
				pC->uwX = x;
				pC->uwY = y;
				pC->uwOrig = uwTile;
				pC->ubState = CRUMBLE_IDLE;
				pC->ubTimer = 0;
			}
			else if(ubCol == COL_ONCE && s_uwOnceCount < TERRAIN_MAX_ONCE) {
				tOnce *pO = &s_pOnce[s_uwOnceCount++];
				pO->uwX = x;
				pO->uwY = y;
				pO->uwOrig = uwTile;
				pO->ubState = ONCE_IDLE;
			}
		}
	}
	if(s_uwCrumbleCount || s_uwOnceCount) {
		logWrite(
			"Terrain: %u crumble, %u once\n",
			s_uwCrumbleCount, s_uwOnceCount
		);
	}
}

void terrainProcess(void) {
	WORD wLeft, wTop;
	UWORD uwW, uwH;
	playerGetHitbox(&wLeft, &wTop, &uwW, &uwH);
	WORD wRight = wLeft + (WORD)uwW - 1;
	WORD wBelow = wTop + (WORD)uwH;

	for(UWORD i = 0; i < s_uwCrumbleCount; ++i) {
		tCrumble *pC = &s_pCrumbles[i];
		UBYTE isOn = (
			playerIsOnGround() &&
			feetOnCell(wLeft, wRight, wBelow, pC->uwX, pC->uwY)
		);
		if(pC->ubState == CRUMBLE_IDLE) {
			if(isOn) {
				pC->ubState = CRUMBLE_ARM;
				pC->ubTimer = CRUMBLE_ARM_FRAMES;
			}
		}
		else if(pC->ubState == CRUMBLE_ARM) {
			if(pC->ubTimer) {
				--pC->ubTimer;
			}
			if(!pC->ubTimer) {
				pC->ubState = CRUMBLE_SHAKE;
				pC->ubTimer = CRUMBLE_SHAKE_FRAMES;
				writeCell(
					pC->uwX, pC->uwY,
					crumbleCrackTile(pC->uwOrig), COL_CRUMBLE
				);
			}
		}
		else if(pC->ubState == CRUMBLE_SHAKE) {
			if(pC->ubTimer) {
				--pC->ubTimer;
			}
			if(!pC->ubTimer) {
				pC->ubState = CRUMBLE_GONE;
				pC->ubTimer = CRUMBLE_RESPAWN_FRAMES;
				writeCell(pC->uwX, pC->uwY, 0, COL_EMPTY);
			}
		}
		else if(pC->ubState == CRUMBLE_GONE) {
			if(pC->ubTimer) {
				--pC->ubTimer;
			}
			if(!pC->ubTimer) {
				pC->ubState = CRUMBLE_IDLE;
				writeCell(pC->uwX, pC->uwY, pC->uwOrig, COL_CRUMBLE);
			}
		}
	}

	for(UWORD i = 0; i < s_uwOnceCount; ++i) {
		tOnce *pO = &s_pOnce[i];
		if(pO->ubState == ONCE_GONE) {
			continue;
		}
		UBYTE isOn = (
			playerIsOnGround() &&
			feetOnCell(wLeft, wRight, wBelow, pO->uwX, pO->uwY)
		);
		if(pO->ubState == ONCE_IDLE) {
			if(isOn) {
				pO->ubState = ONCE_RIDING;
			}
		}
		else if(pO->ubState == ONCE_RIDING && !isOn) {
			pO->ubState = ONCE_GONE;
			writeCell(pO->uwX, pO->uwY, 0, COL_EMPTY);
		}
	}
}

static UBYTE tileInBufferWindow(UWORD uwTileX, UWORD uwTileY) {
	UBYTE ubShift = s_pTileBuffer->ubTileShift;
	UWORD uwCamTileX = s_pTileBuffer->pCamera->uPos.uwX >> ubShift;
	UWORD uwCamTileY = s_pTileBuffer->pCamera->uPos.uwY >> ubShift;
	UWORD uwMarginX = ACE_SCROLLBUFFER_X_MARGIN_SIZE;
	UWORD uwMarginY = s_pTileBuffer->ubYMargin;
	UWORD uwStartX = (uwCamTileX > uwMarginX) ? (UWORD)(uwCamTileX - uwMarginX) : 0;
	UWORD uwStartY = (uwCamTileY > uwMarginY) ? (UWORD)(uwCamTileY - uwMarginY) : 0;
	UWORD uwEndX = (UWORD)(uwStartX + (s_pTileBuffer->uwMarginedWidth >> ubShift));
	UWORD uwEndY = (UWORD)(uwStartY + (s_pTileBuffer->uwMarginedHeight >> ubShift));
	return (UBYTE)(
		uwTileX >= uwStartX && uwTileX < uwEndX &&
		uwTileY >= uwStartY && uwTileY < uwEndY
	);
}

void terrainFlush(void) {
	if(!s_pTileBuffer) {
		return;
	}
	for(UBYTE i = 0; i < s_ubDirtyCount; ++i) {
		if(!tileInBufferWindow(s_pDirtyX[i], s_pDirtyY[i])) {
			continue;
		}
		tileBufferInvalidateTile(s_pTileBuffer, s_pDirtyX[i], s_pDirtyY[i]);
	}
	s_ubDirtyCount = 0;
	// Invalidate only queues both double-buffer states. tileBufferProcess
	// never drains that queue — ACE expects tileBufferQueueProcess() once
	// per loop. Drain this frame's back buffer; the other state's leftovers
	// are stamped after the next swap so BOBs restore from an updated pristine.
	while(s_pTileBuffer->pRedrawStates[s_pTileBuffer->ubStateIdx].ubPendingCount) {
		tileBufferQueueProcess(s_pTileBuffer);
	}
}

void terrainReset(void) {
	s_ubDirtyCount = 0;
	for(UWORD i = 0; i < s_uwCrumbleCount; ++i) {
		tCrumble *pC = &s_pCrumbles[i];
		pC->ubState = CRUMBLE_IDLE;
		pC->ubTimer = 0;
		if(s_pTileBuffer) {
			s_pTileBuffer->pTileData[pC->uwX][pC->uwY] = pC->uwOrig;
		}
		if(g_ubColMap) {
			g_ubColMap[colIndex(pC->uwX, pC->uwY)] = COL_CRUMBLE;
		}
	}
	for(UWORD i = 0; i < s_uwOnceCount; ++i) {
		tOnce *pO = &s_pOnce[i];
		pO->ubState = ONCE_IDLE;
		if(s_pTileBuffer) {
			s_pTileBuffer->pTileData[pO->uwX][pO->uwY] = pO->uwOrig;
		}
		if(g_ubColMap) {
			g_ubColMap[colIndex(pO->uwX, pO->uwY)] = COL_ONCE;
		}
	}
}

void terrainDestroy(void) {
	s_pTileBuffer = 0;
	s_uwCrumbleCount = 0;
	s_uwOnceCount = 0;
	s_ubDirtyCount = 0;
}
