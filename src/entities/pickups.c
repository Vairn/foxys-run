/* Cherry, gem, and heart pickups as BOBs. */
#include "entities/pickups.h"
#include "player/player.h"
#include "entities/effects.h"
#include "assets/assets.h"
#include "game/session.h"
#include "audio/sfx.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include "debug/serial_log.h"
#include <ace/managers/rand.h>
#include <ace/utils/bitmap.h>

#include <sprites/cherry.h>
#include <sprites/gem.h>
#include <sprites/carrot.h>
#include <sprites/star.h>
#include <sprites/acorn.h>
#include "world/level.h"

static UBYTE pickupIsGem(UBYTE ubType) {
	return (UBYTE)(ubType == PICKUP_TYPE_GEM || ubType == PICKUP_TYPE_STAR);
}

// Cherry: 5-frame source strip, ping-ponged in software (0..4..0).
// Gem: mirrored loop baked into the strip (see MIRROR_LOOP_SPRITES).
#define ANIM_DELAY 5       // ticks per frame @50Hz (~10fps)

#define HEART_DROP_MAX 2
#define HEART_FRAME_W 16
#define HEART_FRAME_H 8
#define HEART_ART_W 8
#define HEART_ART_H 7
#define HEART_COL 9          // playfield red
#define HEART_DROP_ODDS 8    // 1 in 8

typedef struct {
	tBob sBob;
	UWORD uwCenterX; // world pixel position of the pickup centre
	UWORD uwCenterY;
	UBYTE ubType;    // PICKUP_TYPE_CHERRY / PICKUP_TYPE_GEM
	UBYTE isActive;  // cleared once collected
	// Precomputed AABB extents (cached at init so we skip per-frame divs).
	WORD wHitLeft;
	WORD wHitTop;
	WORD wHitRight;
	WORD wHitBottom;
} tPickup;

static tTileBufferManager *s_pTileBuffer;

static tBitMap *s_pCherry, *s_pCherryMask;
static tBitMap *s_pGem, *s_pGemMask;
static tBitMap *s_pCarrot, *s_pCarrotMask;
static tBitMap *s_pStar, *s_pStarMask;
static tBitMap *s_pAcorn, *s_pAcornMask;

// Per-frame data/mask addresses, precomputed for each pickup sprite.
static UBYTE *s_pCherryFrame[CHERRY_FRAME_COUNT];
static UBYTE *s_pCherryMaskAddr[CHERRY_FRAME_COUNT];
static UBYTE *s_pGemFrame[GEM_FRAME_COUNT];
static UBYTE *s_pGemMaskAddr[GEM_FRAME_COUNT];
static UBYTE *s_pCarrotFrame[CARROT_FRAME_COUNT];
static UBYTE *s_pCarrotMaskAddr[CARROT_FRAME_COUNT];
static UBYTE *s_pStarFrame[STAR_FRAME_COUNT];
static UBYTE *s_pStarMaskAddr[STAR_FRAME_COUNT];
static UBYTE *s_pAcornFrame[ACORN_FRAME_COUNT];
static UBYTE *s_pAcornMaskAddr[ACORN_FRAME_COUNT];
static UBYTE s_ubCarrotFrame;
static UBYTE s_ubStarFrame;
static UBYTE s_ubAcornFrame;

static tPickup s_pPickups[LEVEL_MAX_PICKUPS];
static tPickup s_pHearts[HEART_DROP_MAX];
static tBitMap *s_pHeart, *s_pHeartMask;
static tRandManager s_sRand;
static UBYTE s_ubCherryFrame;
static BYTE s_bCherryDir; // +1 forward / -1 reverse for ping-pong
static UBYTE s_ubGemFrame;
static UBYTE s_ubAnimTimer;
static UWORD s_uwAmmo;
static UWORD s_uwGemsCollected;
static UWORD s_uwCherriesCollected;
static UWORD s_uwGemTotal;
static UWORD s_uwPickupCount;
static UWORD s_uwPlacedCount;
static UWORD s_uwBossGemSlots;
static UWORD s_uwRemaining;

// Same 8x7 silhouette as the HUD hearts, MSB = leftmost pixel.
static const UBYTE s_pHeartBits[HEART_ART_H] = {
	0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};

static void plotPlayfieldPixel(tBitMap *pBm, UWORD uwX, UWORD uwY, UBYTE ubColor) {
	UWORD uwPlaneRow = pBm->BytesPerRow / pBm->Depth;
	ULONG ulRow = (ULONG)uwY * pBm->BytesPerRow;
	UBYTE ubBit = (UBYTE)(0x80 >> (uwX & 7));
	UBYTE ubOffsX = (UBYTE)(uwX >> 3);
	for(UBYTE ubPlane = 0; ubPlane < pBm->Depth; ++ubPlane) {
		UBYTE *p = (UBYTE *)pBm->Planes[0] + ulRow + (ULONG)ubPlane * uwPlaneRow + ubOffsX;
		if(ubColor & (UBYTE)(1 << ubPlane)) {
			*p |= ubBit;
		}
		else {
			*p &= (UBYTE)~ubBit;
		}
	}
}

static void buildHeartBitmaps(void) {
	s_pHeart = bitmapCreate(HEART_FRAME_W, HEART_FRAME_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	s_pHeartMask = bitmapCreate(HEART_FRAME_W, HEART_FRAME_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	UWORD uwX0 = (HEART_FRAME_W - HEART_ART_W) / 2;
	for(UBYTE gy = 0; gy < HEART_ART_H; ++gy) {
		UBYTE ubRow = s_pHeartBits[gy];
		for(UBYTE gx = 0; gx < HEART_ART_W; ++gx) {
			if(ubRow & (UBYTE)(0x80 >> gx)) {
				plotPlayfieldPixel(s_pHeart, uwX0 + gx, gy, HEART_COL);
				plotPlayfieldPixel(s_pHeartMask, uwX0 + gx, gy, 0xF);
			}
		}
	}
}

static void heartPlace(tPickup *pHeart, WORD wCenterX, WORD wCenterY) {
	pHeart->uwCenterX = (UWORD)wCenterX;
	pHeart->uwCenterY = (UWORD)wCenterY;
	WORD wHalfW = HEART_ART_W / 2;
	WORD wHalfH = HEART_ART_H / 2;
	pHeart->wHitLeft = wCenterX - wHalfW;
	pHeart->wHitTop = wCenterY - wHalfH;
	pHeart->wHitRight = wCenterX + wHalfW;
	pHeart->wHitBottom = wCenterY + wHalfH;
	WORD wX = wCenterX - (HEART_FRAME_W / 2);
	WORD wY = wCenterY - (HEART_FRAME_H / 2);
	if(wX < 0) { wX = 0; }
	if(wY < 0) { wY = 0; }
	pHeart->sBob.sPos.uwX = (UWORD)wX;
	pHeart->sBob.sPos.uwY = (UWORD)wY;
	pHeart->isActive = 1;
}

static void pickupPlaceGem(tPickup *pPickup, WORD wCx, WORD wCy) {
	pPickup->ubType = PICKUP_TYPE_GEM;
	pPickup->uwCenterX = (UWORD)wCx;
	pPickup->uwCenterY = (UWORD)wCy;
	WORD wHalfW = GEM_FRAME_W_ART / 2;
	WORD wHalfH = GEM_FRAME_H / 2;
	pPickup->wHitLeft = wCx - wHalfW;
	pPickup->wHitTop = wCy - wHalfH;
	pPickup->wHitRight = wCx + wHalfW;
	pPickup->wHitBottom = wCy + wHalfH;
	pPickup->sBob.sPos.uwX = (UWORD)(wCx - wHalfW);
	pPickup->sBob.sPos.uwY = (UWORD)(wCy - wHalfH);
}

void pickupsLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;

	UBYTE isNeedCherry = 0;
	UBYTE isNeedGem = 0;
	UBYTE isNeedCarrot = 0;
	UBYTE isNeedStar = 0;
	UBYTE isNeedAcorn = 0;
	s_uwBossGemSlots = 0;
	const tLevelEntity *pDefs = levelGetPickups();
	UWORD uwN = levelGetPickupCount();
	for(UWORD i = 0; i < uwN; ++i) {
		if(pDefs[i].ubType == PICKUP_TYPE_GEM) {
			isNeedGem = 1;
		}
		else if(pDefs[i].ubType == PICKUP_TYPE_STAR) {
			isNeedStar = 1;
		}
		else if(pDefs[i].ubType == PICKUP_TYPE_CARROT) {
			isNeedCarrot = 1;
		}
		else if(pDefs[i].ubType == PICKUP_TYPE_ACORN) {
			isNeedAcorn = 1;
		}
		else {
			isNeedCherry = 1;
		}
	}
	const tLevelEntity *pEnemies = levelGetEnemies();
	UWORD uwE = levelGetEnemyCount();
	for(UWORD i = 0; i < uwE; ++i) {
		if(pEnemies[i].ubType == ENEMY_TYPE_BOSS
			|| pEnemies[i].ubType == ENEMY_TYPE_BOSS_WOODS) {
			isNeedGem = 1;
			s_uwBossGemSlots = 1;
			break;
		}
	}

	s_pCherry = 0;
	s_pCherryMask = 0;
	s_pGem = 0;
	s_pGemMask = 0;
	s_pCarrot = 0;
	s_pCarrotMask = 0;
	s_pStar = 0;
	s_pStarMask = 0;
	s_pAcorn = 0;
	s_pAcornMask = 0;
	if(isNeedCherry) {
		if(levelIsWinter()) {
			s_pCherry = sunnyLoadBitmap("data/sprites/cherry_w.bm", 0);
			s_pCherryMask = sunnyLoadBitmap("data/sprites/cherry_w_mask.bm", 0);
		}
		else {
			char szBm[LEVEL_PATH_MAX];
			char szMask[LEVEL_PATH_MAX];
			levelSpritePath(szBm, "cherry.bm");
			levelSpritePath(szMask, "cherry_mask.bm");
			s_pCherry = sunnyLoadBitmap(szBm, 0);
			s_pCherryMask = sunnyLoadBitmap(szMask, 0);
		}
	}
	if(isNeedGem) {
		if(levelIsWinter()) {
			s_pGem = sunnyLoadBitmap("data/sprites/gem_w.bm", 0);
			s_pGemMask = sunnyLoadBitmap("data/sprites/gem_w_mask.bm", 0);
		}
		else {
			char szBm[LEVEL_PATH_MAX];
			char szMask[LEVEL_PATH_MAX];
			levelSpritePath(szBm, "gem.bm");
			levelSpritePath(szMask, "gem_mask.bm");
			s_pGem = sunnyLoadBitmap(szBm, 0);
			s_pGemMask = sunnyLoadBitmap(szMask, 0);
		}
	}

	if(s_pCherry) {
		for(UBYTE i = 0; i < CHERRY_FRAME_COUNT; ++i) {
			s_pCherryFrame[i] = bobCalcFrameAddress(s_pCherry, i * CHERRY_FRAME_H);
			s_pCherryMaskAddr[i] = bobCalcFrameAddress(s_pCherryMask, i * CHERRY_FRAME_H);
		}
	}
	if(s_pGem) {
		for(UBYTE i = 0; i < GEM_FRAME_COUNT; ++i) {
			s_pGemFrame[i] = bobCalcFrameAddress(s_pGem, i * GEM_FRAME_H);
			s_pGemMaskAddr[i] = bobCalcFrameAddress(s_pGemMask, i * GEM_FRAME_H);
		}
	}
	if(isNeedCarrot) {
		char szBm[LEVEL_PATH_MAX];
		char szMask[LEVEL_PATH_MAX];
		levelSpritePath(szBm, "carrot.bm");
		levelSpritePath(szMask, "carrot_mask.bm");
		s_pCarrot = sunnyLoadBitmap(szBm, 0);
		s_pCarrotMask = sunnyLoadBitmap(szMask, 0);
		for(UBYTE i = 0; i < CARROT_FRAME_COUNT; ++i) {
			s_pCarrotFrame[i] = bobCalcFrameAddress(s_pCarrot, i * CARROT_FRAME_H);
			s_pCarrotMaskAddr[i] = bobCalcFrameAddress(s_pCarrotMask, i * CARROT_FRAME_H);
		}
	}
	if(isNeedStar) {
		char szBm[LEVEL_PATH_MAX];
		char szMask[LEVEL_PATH_MAX];
		levelSpritePath(szBm, "star.bm");
		levelSpritePath(szMask, "star_mask.bm");
		s_pStar = sunnyLoadBitmap(szBm, 0);
		s_pStarMask = sunnyLoadBitmap(szMask, 0);
		for(UBYTE i = 0; i < STAR_FRAME_COUNT; ++i) {
			s_pStarFrame[i] = bobCalcFrameAddress(s_pStar, i * STAR_FRAME_H);
			s_pStarMaskAddr[i] = bobCalcFrameAddress(s_pStarMask, i * STAR_FRAME_H);
		}
	}
	if(isNeedAcorn) {
		char szBm[LEVEL_PATH_MAX];
		char szMask[LEVEL_PATH_MAX];
		levelSpritePath(szBm, "acorn.bm");
		levelSpritePath(szMask, "acorn_mask.bm");
		s_pAcorn = sunnyLoadBitmap(szBm, 0);
		s_pAcornMask = sunnyLoadBitmap(szMask, 0);
		for(UBYTE i = 0; i < ACORN_FRAME_COUNT; ++i) {
			s_pAcornFrame[i] = bobCalcFrameAddress(s_pAcorn, i * ACORN_FRAME_H);
			s_pAcornMaskAddr[i] = bobCalcFrameAddress(s_pAcornMask, i * ACORN_FRAME_H);
		}
	}

	s_ubCherryFrame = 0;
	s_bCherryDir = 1;
	s_ubGemFrame = 0;
	s_ubCarrotFrame = 0;
	s_ubStarFrame = 0;
	s_ubAcornFrame = 0;
	s_ubAnimTimer = 0;
	s_uwGemsCollected = 0;
	s_uwCherriesCollected = 0;
	s_uwGemTotal = 0;
	s_uwRemaining = 0;
	buildHeartBitmaps();
	randInit(&s_sRand, (UWORD)(0xA501 + levelGetIndex() * 17), 0xC0DE);
}

void pickupsInitBobs(void) {
	s_uwPlacedCount = levelGetPickupCount();
	if(s_uwPlacedCount > LEVEL_MAX_PICKUPS) {
		s_uwPlacedCount = LEVEL_MAX_PICKUPS;
	}
	s_uwPickupCount = s_uwPlacedCount + s_uwBossGemSlots;
	if(s_uwPickupCount > LEVEL_MAX_PICKUPS) {
		s_uwPickupCount = LEVEL_MAX_PICKUPS;
		if(s_uwPlacedCount > s_uwPickupCount) {
			s_uwPlacedCount = s_uwPickupCount;
		}
	}
	s_uwRemaining = s_uwPlacedCount;
	s_uwGemTotal = 0;
	s_uwGemsCollected = 0;
	s_uwCherriesCollected = 0;
	s_uwAmmo = sessionGetAmmo();
	const tLevelEntity *pDefs = levelGetPickups();
	for(UWORD i = 0; i < s_uwPlacedCount; ++i) {
		tPickup *pPickup = &s_pPickups[i];
		const tLevelEntity *pDef = &pDefs[i];
		pPickup->ubType = pDef->ubType;
		pPickup->isActive = 1;
		if(pickupIsGem(pPickup->ubType)) {
			++s_uwGemTotal;
		}
		// The converter stored the pickup centre as (tile) = floor(centre/16),
		// so reconstruct the centre at the tile's midpoint.
		pPickup->uwCenterX = (pDef->uwTileX << TILE_SHIFT) + (TILE_SIZE / 2);
		pPickup->uwCenterY = (pDef->uwTileY << TILE_SHIFT) + (TILE_SIZE / 2);

		UWORD uwFrameW, uwFrameH, uwArtW;
		UBYTE *pFrame, *pMask;
		WORD wHalfW, wHalfH;
		if(pPickup->ubType == PICKUP_TYPE_GEM) {
			uwFrameW = GEM_FRAME_W; uwFrameH = GEM_FRAME_H; uwArtW = GEM_FRAME_W_ART;
			pFrame = s_pGemFrame[0]; pMask = s_pGemMaskAddr[0];
			wHalfW = GEM_FRAME_W_ART / 2; wHalfH = GEM_FRAME_H / 2;
		}
		else if(pPickup->ubType == PICKUP_TYPE_STAR) {
			uwFrameW = STAR_FRAME_W; uwFrameH = STAR_FRAME_H; uwArtW = STAR_FRAME_W_ART;
			pFrame = s_pStarFrame[0]; pMask = s_pStarMaskAddr[0];
			wHalfW = STAR_FRAME_W_ART / 2; wHalfH = STAR_FRAME_H / 2;
		}
		else if(pPickup->ubType == PICKUP_TYPE_CARROT) {
			uwFrameW = CARROT_FRAME_W; uwFrameH = CARROT_FRAME_H; uwArtW = CARROT_FRAME_W_ART;
			pFrame = s_pCarrotFrame[0]; pMask = s_pCarrotMaskAddr[0];
			wHalfW = CARROT_FRAME_W_ART / 2; wHalfH = CARROT_FRAME_H / 2;
		}
		else if(pPickup->ubType == PICKUP_TYPE_ACORN) {
			uwFrameW = ACORN_FRAME_W; uwFrameH = ACORN_FRAME_H; uwArtW = ACORN_FRAME_W_ART;
			pFrame = s_pAcornFrame[0]; pMask = s_pAcornMaskAddr[0];
			wHalfW = ACORN_FRAME_W_ART / 2; wHalfH = ACORN_FRAME_H / 2;
		}
		else {
			uwFrameW = CHERRY_FRAME_W; uwFrameH = CHERRY_FRAME_H; uwArtW = CHERRY_FRAME_W_ART;
			pFrame = s_pCherryFrame[0]; pMask = s_pCherryMaskAddr[0];
			wHalfW = CHERRY_FRAME_W_ART / 2; wHalfH = CHERRY_FRAME_H / 2;
		}

		// Cache the AABB so we never recompute it per frame.
		WORD wCx = (WORD)pPickup->uwCenterX;
		WORD wCy = (WORD)pPickup->uwCenterY;
		pPickup->wHitLeft   = wCx - wHalfW;
		pPickup->wHitTop    = wCy - wHalfH;
		pPickup->wHitRight  = wCx + wHalfW;
		pPickup->wHitBottom = wCy + wHalfH;

		// Top-left so the (left-aligned) art is centred over the tile.
		UWORD uwX = pPickup->uwCenterX - (uwArtW / 2);
		UWORD uwY = pPickup->uwCenterY - (uwFrameH / 2);
		bobInit(&pPickup->sBob, uwFrameW, uwFrameH, 1, pFrame, pMask, uwX, uwY);
	}

	for(UWORD i = 0; i < s_uwBossGemSlots; ++i) {
		UWORD uwSlot = s_uwPlacedCount + i;
		if(uwSlot >= LEVEL_MAX_PICKUPS || !s_pGem) {
			break;
		}
		tPickup *pGem = &s_pPickups[uwSlot];
		pGem->isActive = 0;
		++s_uwGemTotal;
		bobInit(
			&pGem->sBob, GEM_FRAME_W, GEM_FRAME_H, 1,
			s_pGemFrame[0], s_pGemMaskAddr[0], 0, 0
		);
		pickupPlaceGem(pGem, 0, 0);
		pGem->isActive = 0;
	}

	if(s_pHeart && s_pHeartMask) {
		UBYTE *pHeartFrame = (UBYTE *)s_pHeart->Planes[0];
		UBYTE *pHeartMask = (UBYTE *)s_pHeartMask->Planes[0];
		for(UBYTE i = 0; i < HEART_DROP_MAX; ++i) {
			tPickup *pHeart = &s_pHearts[i];
			pHeart->isActive = 0;
			bobInit(
				&pHeart->sBob, HEART_FRAME_W, HEART_FRAME_H, 1,
				pHeartFrame, pHeartMask, 0, 0
			);
		}
	}
}

void pickupsReset(void) {
	s_uwGemsCollected = 0;
	s_uwCherriesCollected = 0;
	s_uwAmmo = sessionGetAmmo();
	s_uwRemaining = 0;
	s_uwGemTotal = 0;
	s_ubCherryFrame = 0;
	s_bCherryDir = 1;
	s_ubGemFrame = 0;
	s_ubCarrotFrame = 0;
	s_ubStarFrame = 0;
	s_ubAcornFrame = 0;
	s_ubAnimTimer = 0;
	for(UWORD i = 0; i < s_uwPlacedCount; ++i) {
		s_pPickups[i].isActive = 1;
		if(pickupIsGem(s_pPickups[i].ubType)) {
			++s_uwGemTotal;
		}
		++s_uwRemaining;
	}
	for(UWORD i = 0; i < s_uwBossGemSlots; ++i) {
		UWORD uwSlot = s_uwPlacedCount + i;
		if(uwSlot >= s_uwPickupCount) {
			break;
		}
		s_pPickups[uwSlot].isActive = 0;
		++s_uwGemTotal;
	}
	for(UBYTE i = 0; i < HEART_DROP_MAX; ++i) {
		s_pHearts[i].isActive = 0;
	}
}

void pickupsProcess(void) {
	// Player hitbox in world pixels for overlap testing.
	WORD wPlayerX, wPlayerY;
	UWORD uwPlayerW, uwPlayerH;
	playerGetHitbox(&wPlayerX, &wPlayerY, &uwPlayerW, &uwPlayerH);
	WORD wPlayerR = wPlayerX + (WORD)uwPlayerW - 1;
	WORD wPlayerB = wPlayerY + (WORD)uwPlayerH - 1;

	for(UBYTE i = 0; i < HEART_DROP_MAX; ++i) {
		tPickup *pHeart = &s_pHearts[i];
		if(!pHeart->isActive) {
			continue;
		}
		if(pHeart->wHitLeft <= wPlayerR && pHeart->wHitRight >= wPlayerX
			&& pHeart->wHitTop <= wPlayerB && pHeart->wHitBottom >= wPlayerY) {
			if(!playerHeal()) {
				continue; // already at max — leave it on the ground
			}
			pHeart->isActive = 0;
			effectsSpawnPickup(
				(WORD)pHeart->uwCenterX, (WORD)pHeart->uwCenterY
			);
			sfxPlayGem();
		}
	}

	if(!s_uwRemaining) {
		return; // no cherries/gems left to animate or test
	}

	// Advance cherry (ping-pong) and gem (linear loop) on a shared timer.
	UBYTE ubFrameChanged = 0;
	if(++s_ubAnimTimer >= ANIM_DELAY) {
		s_ubAnimTimer = 0;
		ubFrameChanged = 1;

		if(s_pCherry) {
			// Cherry: 0..N-1..0 without dwelling an extra tick on the ends.
			s_ubCherryFrame = (UBYTE)((BYTE)s_ubCherryFrame + s_bCherryDir);
			if(s_ubCherryFrame == 0 || s_ubCherryFrame == CHERRY_FRAME_COUNT - 1) {
				s_bCherryDir = (BYTE)-s_bCherryDir;
			}
		}

		if(++s_ubGemFrame >= GEM_FRAME_COUNT) {
			s_ubGemFrame = 0;
		}
		if(++s_ubStarFrame >= STAR_FRAME_COUNT) {
			s_ubStarFrame = 0;
		}
		if(++s_ubCarrotFrame >= CARROT_FRAME_COUNT) {
			s_ubCarrotFrame = 0;
		}
		if(++s_ubAcornFrame >= ACORN_FRAME_COUNT) {
			s_ubAcornFrame = 0;
		}
	}

	for(UWORD i = 0; i < s_uwPickupCount; ++i) {
		tPickup *pPickup = &s_pPickups[i];
		if(!pPickup->isActive) {
			continue;
		}

		// AABB overlap with the player  uses precomputed bounds.
		if(pPickup->wHitLeft <= wPlayerR && pPickup->wHitRight >= wPlayerX
			&& pPickup->wHitTop <= wPlayerB && pPickup->wHitBottom >= wPlayerY) {
			pPickup->isActive = 0;
			--s_uwRemaining;
			if(pickupIsGem(pPickup->ubType)) {
				++s_uwGemsCollected;
			}
			else {
				++s_uwCherriesCollected;
				++s_uwAmmo;
				sessionSetAmmo(s_uwAmmo);
			}
			effectsSpawnPickup(
				(WORD)pPickup->uwCenterX, (WORD)pPickup->uwCenterY
			);
			sfxPlayGem();
			continue;
		}

		// Only update the bob frame data when the animation actually advances.
		if(ubFrameChanged) {
			if(pPickup->ubType == PICKUP_TYPE_GEM) {
				bobSetFrame(
					&pPickup->sBob, s_pGemFrame[s_ubGemFrame],
					s_pGemMaskAddr[s_ubGemFrame]
				);
			}
			else if(pPickup->ubType == PICKUP_TYPE_STAR && s_pStar) {
				bobSetFrame(
					&pPickup->sBob, s_pStarFrame[s_ubStarFrame],
					s_pStarMaskAddr[s_ubStarFrame]
				);
			}
			else if(pPickup->ubType == PICKUP_TYPE_CARROT && s_pCarrot) {
				bobSetFrame(
					&pPickup->sBob, s_pCarrotFrame[s_ubCarrotFrame],
					s_pCarrotMaskAddr[s_ubCarrotFrame]
				);
			}
			else if(pPickup->ubType == PICKUP_TYPE_ACORN && s_pAcorn) {
				bobSetFrame(
					&pPickup->sBob, s_pAcornFrame[s_ubAcornFrame],
					s_pAcornMaskAddr[s_ubAcornFrame]
				);
			}
			else if(s_pCherry) {
				bobSetFrame(
					&pPickup->sBob, s_pCherryFrame[s_ubCherryFrame],
					s_pCherryMaskAddr[s_ubCherryFrame]
				);
			}
		}
	}
}

void pickupsPushBobs(void) {
	UWORD uwCamX = s_pTileBuffer->pCamera->uPos.uwX;
	UWORD uwCamY = s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewL = (WORD)uwCamX - CULL_MARGIN;
	WORD wViewT = (WORD)uwCamY - CULL_MARGIN;
	WORD wViewR = (WORD)uwCamX + SCREEN_W + CULL_MARGIN;
	WORD wViewB = (WORD)uwCamY + SCREEN_H + CULL_MARGIN;

	if(s_uwRemaining) {
		for(UWORD i = 0; i < s_uwPickupCount; ++i) {
			tPickup *pPickup = &s_pPickups[i];
			if(!pPickup->isActive) {
				continue;
			}
			WORD wX = (WORD)pPickup->sBob.sPos.uwX;
			WORD wY = (WORD)pPickup->sBob.sPos.uwY;
			if(wX > wViewR || wY > wViewB
				|| (wX + (WORD)pPickup->sBob.uwWidth) < wViewL
				|| (wY + (WORD)pPickup->sBob.uwHeight) < wViewT) {
				continue;
			}
			bobPush(&pPickup->sBob);
		}
	}

	for(UBYTE i = 0; i < HEART_DROP_MAX; ++i) {
		tPickup *pHeart = &s_pHearts[i];
		if(!pHeart->isActive) {
			continue;
		}
		WORD wX = (WORD)pHeart->sBob.sPos.uwX;
		WORD wY = (WORD)pHeart->sBob.sPos.uwY;
		if(wX > wViewR || wY > wViewB
			|| (wX + (WORD)pHeart->sBob.uwWidth) < wViewL
			|| (wY + (WORD)pHeart->sBob.uwHeight) < wViewT) {
			continue;
		}
		bobPush(&pHeart->sBob);
	}
}

void pickupsDestroy(void) {
	bitmapDestroy(s_pCherry);
	bitmapDestroy(s_pCherryMask);
	bitmapDestroy(s_pGem);
	bitmapDestroy(s_pGemMask);
	bitmapDestroy(s_pCarrot);
	bitmapDestroy(s_pCarrotMask);
	bitmapDestroy(s_pStar);
	bitmapDestroy(s_pStarMask);
	bitmapDestroy(s_pAcorn);
	bitmapDestroy(s_pAcornMask);
	bitmapDestroy(s_pHeart);
	bitmapDestroy(s_pHeartMask);
	s_pCarrot = 0; s_pCarrotMask = 0;
	s_pStar = 0; s_pStarMask = 0;
	s_pAcorn = 0; s_pAcornMask = 0;
}

UWORD pickupsGetAmmo(void) {
	return s_uwAmmo;
}

UBYTE pickupsTrySpendAmmo(void) {
	if(!s_uwAmmo) {
		return 0;
	}
	--s_uwAmmo;
	sessionSetAmmo(s_uwAmmo);
	return 1;
}

UWORD pickupsGetCherriesCollected(void) {
	return s_uwCherriesCollected;
}

UWORD pickupsGetGemsCollected(void) {
	return s_uwGemsCollected;
}

UWORD pickupsGetGemsRemaining(void) {
	return (s_uwGemsCollected < s_uwGemTotal)
		? (UWORD)(s_uwGemTotal - s_uwGemsCollected) : 0;
}

UBYTE pickupsAllGemsCollected(void) {
	return s_uwGemsCollected >= s_uwGemTotal;
}

void pickupsSpawnDroppedGems(WORD wCenterX, WORD wCenterY) {
	for(UWORD i = 0; i < s_uwBossGemSlots; ++i) {
		UWORD uwSlot = s_uwPlacedCount + i;
		if(uwSlot >= s_uwPickupCount) {
			break;
		}
		tPickup *pGem = &s_pPickups[uwSlot];
		if(pGem->isActive) {
			continue;
		}
		pickupPlaceGem(pGem, wCenterX + (WORD)(i * 12), wCenterY);
		pGem->isActive = 1;
		++s_uwRemaining;
	}
}

void pickupsTryDropHeart(WORD wCenterX, WORD wCenterY) {
	if(randUwMax(&s_sRand, HEART_DROP_ODDS - 1)) {
		return; // 7 in 8: no drop
	}
	for(UBYTE i = 0; i < HEART_DROP_MAX; ++i) {
		tPickup *pHeart = &s_pHearts[i];
		if(pHeart->isActive) {
			continue;
		}
		heartPlace(pHeart, wCenterX, wCenterY);
		return;
	}
}
