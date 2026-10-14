/* SunnyLand cherry-pip projectiles.
 *
 * Two concurrent pips on hardware sprite channels 4 and 5 (HUD owns 0-3 for
 * the overlay plus the chained player fox, and 6 for the lives icon+count).
 * 2BPP seed plotted into its own CHIP bitmap. Shots arc with player gravity,
 * despawn on solid/slope tiles, off-screen, or after killing an enemy.
 */
#include "shots.h"
#include "player.h"
#include "pickups.h"
#include "monsters.h"
#include "sfx.h"
#include "switches.h"
#include "doors.h"

#include <ace/managers/sprite.h>
#include <ace/managers/copper.h>
#include <ace/utils/custom.h>
#include <ace/utils/bitmap.h>
#include "collision.h"
#include "game_defs.h"
#include "sprite_plot.h"
#include <string.h>

#define SHOT_COUNT 2
#define SHOT_CHANNEL0 4

#define SHOT_VX 1280       // ~5 px/frame
#define SHOT_VY_LAUNCH -1536 // ~6 px/frame upward
#define SHOT_GRAVITY 128   // matches the player
#define SHOT_MAX_FALL 2560

#define PIP_W 8
#define PIP_H 7

#define COL_PIP_DARK 0x520
#define COL_PIP_BODY 0xA74
#define COL_PIP_HIGH 0xDC9

typedef struct {
	tSprite *pSprite;
	tBitMap *pBitmap;
	LONG lPosX;
	LONG lPosY;
	LONG lVelX;
	LONG lVelY;
	UBYTE isActive;
} tShot;

static tView *s_pView;
static tTileBufferManager *s_pTileBuffer;
static tCopBlock *s_pPaletteBlock;
static tShot s_pShots[SHOT_COUNT];

// 8x7 seed: 1 = dark rim, 2 = body, 3 = highlight.
static const UBYTE s_pPip[PIP_H][PIP_W] = {
	{0, 0, 1, 1, 1, 1, 0, 0},
	{0, 1, 2, 3, 2, 2, 1, 0},
	{1, 2, 3, 2, 2, 2, 2, 1},
	{1, 2, 2, 2, 2, 2, 2, 1},
	{1, 2, 2, 2, 2, 2, 1, 0},
	{0, 1, 2, 2, 2, 1, 0, 0},
	{0, 0, 1, 1, 1, 0, 0, 0},
};

static void plotPip(tBitMap *pBm) {
	memset(
		(UBYTE *)pBm->Planes[0] + SPR_ROW_STRIDE, 0,
		(ULONG)(pBm->Rows - 1) * SPR_ROW_STRIDE
	);
	for(UBYTE gy = 0; gy < PIP_H; ++gy) {
		for(UBYTE gx = 0; gx < PIP_W; ++gx) {
			UBYTE ubColor = s_pPip[gy][gx];
			if(ubColor) {
				sprPlot(pBm, gx, gy, ubColor);
			}
		}
	}
}

static UBYTE pipHitsWall(WORD wX, WORD wY) {
	return colIsBlocking(colAt(wX, wY));
}

static void shotDisable(tShot *pShot) {
	pShot->isActive = 0;
	if(pShot->pSprite->isEnabled) {
		spriteSetEnabled(pShot->pSprite, 0);
	}
}

static void shotTryFire(void) {
	if(!playerWantsFire() || !playerGetHealth()) {
		return;
	}
	for(UBYTE i = 0; i < SHOT_COUNT; ++i) {
		tShot *pShot = &s_pShots[i];
		if(pShot->isActive) {
			continue;
		}
		if(!pickupsTrySpendAmmo()) {
			return;
		}
		WORD wSpitX, wSpitY;
		playerGetSpitPos(&wSpitX, &wSpitY);
		pShot->lPosX = PX(wSpitX - PIP_W / 2);
		pShot->lPosY = PX(wSpitY - PIP_H / 2);
		pShot->lVelX = (playerGetFacing() == PLAYER_FACE_LEFT) ? -SHOT_VX : SHOT_VX;
		pShot->lVelY = SHOT_VY_LAUNCH;
		pShot->isActive = 1;
		spriteSetEnabled(pShot->pSprite, 1);
		sfxPlaySpit();
		return;
	}
}

static void shotIntegrate(tShot *pShot) {
	pShot->lVelY += SHOT_GRAVITY;
	if(pShot->lVelY > SHOT_MAX_FALL) {
		pShot->lVelY = SHOT_MAX_FALL;
	}
	pShot->lPosX += pShot->lVelX;
	pShot->lPosY += pShot->lVelY;

	WORD wX = TO_PX(pShot->lPosX);
	WORD wY = TO_PX(pShot->lPosY);
	WORD wCx = wX + PIP_W / 2;
	WORD wCy = wY + PIP_H / 2;

	if(switchesHitWorldRect(wX, wY, wX + PIP_W - 1, wY + PIP_H - 1)) {
		shotDisable(pShot);
		return;
	}
	if(doorsHitWorldRect(wX, wY, wX + PIP_W - 1, wY + PIP_H - 1)) {
		shotDisable(pShot);
		return;
	}
	if(pipHitsWall(wCx, wCy)) {
		shotDisable(pShot);
		return;
	}
	if(monstersHitWorldRect(wX, wY, wX + PIP_W - 1, wY + PIP_H - 1)) {
		shotDisable(pShot);
		return;
	}

	WORD wCamX = (WORD)s_pTileBuffer->pCamera->uPos.uwX;
	WORD wCamY = (WORD)s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewX = wX - wCamX;
	WORD wViewY = wY - wCamY;
	if(wViewX < -PIP_W || wViewX > SCREEN_W || wViewY < -PIP_H || wViewY > SCREEN_H) {
		shotDisable(pShot);
		return;
	}

	pShot->pSprite->wX = wViewX;
	pShot->pSprite->wY = wViewY;
	spriteRequestMetadataUpdate(pShot->pSprite);
}

void shotsCreate(tView *pView, tTileBufferManager *pTileBuffer) {
	s_pView = pView;
	s_pTileBuffer = pTileBuffer;

	s_pPaletteBlock = copBlockCreate(pView->pCopList, 3, 0, 0);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[25], COL_PIP_DARK);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[26], COL_PIP_BODY);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[27], COL_PIP_HIGH);

	for(UBYTE i = 0; i < SHOT_COUNT; ++i) {
		tShot *pShot = &s_pShots[i];
		pShot->pBitmap = bitmapCreate(SPR_W, PIP_H + 2, 2, BMF_CLEAR | BMF_INTERLEAVED);
		plotPip(pShot->pBitmap);
		pShot->pSprite = spriteAdd((UBYTE)(SHOT_CHANNEL0 + i), pShot->pBitmap);
		pShot->pSprite->wX = 0;
		pShot->pSprite->wY = 0;
		pShot->isActive = 0;
		spriteSetEnabled(pShot->pSprite, 0);
	}
}

void shotsReset(void) {
	for(UBYTE i = 0; i < SHOT_COUNT; ++i) {
		shotDisable(&s_pShots[i]);
	}
}

void shotsProcess(void) {
	shotTryFire();
	for(UBYTE i = 0; i < SHOT_COUNT; ++i) {
		tShot *pShot = &s_pShots[i];
		if(pShot->isActive) {
			shotIntegrate(pShot);
		}
		spriteProcess(pShot->pSprite);
		spriteProcessChannel((UBYTE)(SHOT_CHANNEL0 + i));
	}
}

void shotsDestroy(void) {
	for(UBYTE i = 0; i < SHOT_COUNT; ++i) {
		tShot *pShot = &s_pShots[i];
		if(pShot->pSprite) {
			spriteRemove(pShot->pSprite);
			pShot->pSprite = 0;
		}
		if(pShot->pBitmap) {
			bitmapDestroy(pShot->pBitmap);
			pShot->pBitmap = 0;
		}
		pShot->isActive = 0;
	}
	if(s_pPaletteBlock) {
		copBlockDestroy(s_pView->pCopList, s_pPaletteBlock);
		s_pPaletteBlock = 0;
	}
}
