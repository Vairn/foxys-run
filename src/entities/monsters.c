/* Walker / flyer / hopper / boss enemies as BOBs. */
#include "entities/monsters.h"
#include "entities/pickups.h"
#include "player/player.h"
#include "entities/effects.h"
#include "assets/assets.h"
#include "audio/sfx.h"
#include "game/game_defs.h"

#include <ace/managers/bob.h>
#include "debug/serial_log.h"
#include <ace/utils/bitmap.h>

#include <sprites/oposum.h>
#include <sprites/eagle_attack.h>
#include <sprites/frog_idle.h>
#include <sprites/frog_jump.h>
#include <sprites/fox.h>
#include <sprites/owl.h>
#include <sprites/yeti.h>
#include <sprites/boss.h>
#include <sprites/boss_shot.h>
#include <sprites/beetle.h>
#include <sprites/slug.h>
#include <sprites/slimer.h>
#include <sprites/dog.h>
#include <sprites/ant.h>
#include <sprites/dino.h>
#include <sprites/gator.h>
#include <sprites/vulture.h>
#include <sprites/bee.h>
#include <sprites/grasshopper.h>
#include <sprites/piranha.h>
#include <sprites/piranha_attack.h>
#include <sprites/piranha_ball.h>
#include <sprites/goo.h>
#include <sprites/woods_boss.h>
#include <sprites/woods_boss_shot.h>
#include "world/level.h"
#include "world/collision.h"
#include "props/doors.h"

// Enemies farther than this from the viewport skip AI/animation. Wider than
// CULL_MARGIN so they are already moving when they scroll into view.
#define PROCESS_MARGIN 96

// Facing indices into the per-type frame/mask address tables.
#define FACE_LEFT 0
#define FACE_RIGHT 1

//----------------------------------------------------------------- TUNING (50Hz)

#define MON_GRAVITY 128       // ~0.5 px/frame^2 (matches the sped-up player)
#define MON_MAX_FALL 3200     // ~12.5 px/frame terminal velocity

#define WALKER_SPEED 512     // ~2 px/frame
#define WALKER_ANIM_DELAY 5

#define FLYER_SPEED 640       // ~2.5 px/frame
#define FLYER_VBOB_SPEED 192  // vertical bob rate
#define FLYER_VBOB_RANGE PX(14) // +/- 14 px around the spawn height
#define FLYER_ANIM_DELAY 5

#define HOPPER_HOP_VX 384       // ~1.5 px/frame forward while hopping
#define HOPPER_HOP_VY 1600      // launch speed (upwards)
#define HOPPER_HOP_PERIOD 45    // ground ticks between hops
#define HOPPER_ANIM_DELAY 8

#define BEETLE_SPEED 256
#define SLUG_SPEED 192
#define SLIMER_SPEED 160
#define CHASER_WALK 384
#define CHASER_RUN 768
#define CHASER_RANGE PX(96)
#define CHARGE_WALK 256
#define CHARGE_BURST 1024
#define CHARGE_TELEGRAPH 20
#define CHARGE_GO 28
#define CHARGE_REST 36
#define SWOOP_DIVE 768
#define SWOOP_CLIMB 384
#define LEAPER_HOP_VY 1920
#define LEAPER_HOP_VX 512
#define SHOOTER_PERIOD 70
#define SHOOTER_BALL_VX 768
#define GOO_PERIOD 90
#define GOO_LIFE 90
#define GOO_MAX 2
#define PACK_SHOT_MAX 2
#define WOODS_BOSS_HP_MAX 7
#define WOODS_ANIM_IDLE 0
#define WOODS_ANIM_RUN 1
#define WOODS_ANIM_JUMP 2
#define WOODS_ANIM_THROW 3
#define WOODS_ANIM_HURT 4

// Shared horizontal patrol half-width from spawn X. Keeps enemies in their
// own pocket of the level so open platforms / sky corridors cannot funnel
// several foes into the same stretch.
#define MON_PATROL_RANGE PX(160) // +/- 160 px (~10 tiles)

//------------------------------------------------------------------ STATE

typedef struct {
	tBob sBob;
	LONG lPosX;   // world X of bob top-left, fixed point
	LONG lPosY;   // world Y of bob top-left, fixed point
	LONG lVelX;
	LONG lVelY;
	LONG lHomeX;  // spawn X (horizontal patrol-zone centre), fixed point
	LONG lHomeY;  // spawn height (flyer vertical-bob anchor), fixed point
	WORD wDir;    // facing/movement sign: +1 right, -1 left
	UBYTE ubType; // ENEMY_TYPE_WALKER / FLYER / HOPPER / BOSS
	UBYTE isActive;
	UBYTE ubAnimFrame;
	UBYTE ubAnimTimer;
	UWORD uwTimer; // generic per-type timer (hopper cadence, boss throw)
	UWORD uwHopTimer; // boss hop; throw resets uwTimer before a hop could fire
	UBYTE isAwake; // bosses stay idle until the player is within a screen
	// Hitbox, relative to the bob top-left (pixels).
	UBYTE ubHp;
	UBYTE ubInvuln;
	UBYTE ubAction;
	UBYTE ubHbX, ubHbY, ubHbW, ubHbH;
	// Upward draw offset (px) applied when pushing the bob. Some sprites are
	// drawn with their art low in the frame (forest opossum); lifting it
	// matches hopper clearance without moving the grounded hitbox.
	UBYTE ubDrawRaise;
} tMonster;

static tTileBufferManager *s_pTileBuffer;

// Each type keeps its base art bitmap and a horizontally mirrored (_flip) one,
// each with its own transparency mask.
static tBitMap *s_pWalker, *s_pWalkerMask, *s_pWalkerFlip, *s_pWalkerFlipMask;
static tBitMap *s_pFlyer, *s_pFlyerMask, *s_pFlyerFlip, *s_pFlyerFlipMask;
static tBitMap *s_pHopperIdle, *s_pHopperIdleMask, *s_pHopperIdleFlip, *s_pHopperIdleFlipMask;
static tBitMap *s_pHopperJump, *s_pHopperJumpMask, *s_pHopperJumpFlip, *s_pHopperJumpFlipMask;
static tBitMap *s_pBoss, *s_pBossMask, *s_pBossFlip, *s_pBossFlipMask;
static tBitMap *s_pBossShot, *s_pBossShotMask, *s_pBossShotFlip, *s_pBossShotFlipMask;

static UBYTE *s_pBossFrame[2][BOSS_FRAME_COUNT];
static UBYTE *s_pBossMaskA[2][BOSS_FRAME_COUNT];
static UBYTE *s_pBossShotFrame[2][BOSS_SHOT_FRAME_COUNT];
static UBYTE *s_pBossShotMaskA[2][BOSS_SHOT_FRAME_COUNT];

#define BOSS_HP_MAX 5
#define BOSS_INVULN_FRAMES 40
#define BOSS_SHOT_MAX 2
#define BOSS_ANIM_IDLE 0
#define BOSS_ANIM_RUN 1
#define BOSS_ANIM_JUMP 2
#define BOSS_ANIM_HURT 3
#define BOSS_ANIM_SHOT 4
#define BOSS_ACT_NONE 0
#define BOSS_ACT_THROW 1
#define BOSS_SPEED 384
#define BOSS_HOP_VY 2304      // match player jump so the boss can mount y=8 pads
#define BOSS_THROW_PERIOD 80
#define BOSS_HOP_PERIOD 55
#define BOSS_SHOT_VX 896
#define BOSS_SHOT_VY -1024
#define BOSS_SHOT_GRAVITY 96

typedef struct {
	tBob sBob;
	LONG lPosX;
	LONG lPosY;
	LONG lVelX;
	LONG lVelY;
	WORD wDir;
	UBYTE isActive;
	UBYTE ubAnimFrame;
	UBYTE ubAnimTimer;
} tBossShot;

static tBossShot s_pBossShots[BOSS_SHOT_MAX];
static UBYTE s_ubHasBoss;
static UBYTE s_ubBossAlive;
static UBYTE s_ubBossHp;
static UBYTE s_ubBossHpMax;
static UBYTE s_ubWinterSet;
static UWORD s_uwShotW, s_uwShotH, s_uwShotArtW;
static UBYTE s_ubShotFrames;

#define PACK_FRAME_MAX 8
typedef struct {
	tBitMap *pBm, *pMask, *pFlip, *pFlipMask;
	UBYTE *pFrame[2][PACK_FRAME_MAX];
	UBYTE *pMaskA[2][PACK_FRAME_MAX];
	UBYTE ubCount;
} tPackSheet;

static tPackSheet s_sBeetle, s_sSlug, s_sSlimer, s_sDog, s_sAnt;
static tPackSheet s_sDino, s_sGator, s_sVulture, s_sBee, s_sPiranha, s_sPiranhaAtk;
static UBYTE *s_pHopperPackFrame[2][GRASSHOPPER_FRAME_COUNT];
static UBYTE *s_pHopperPackMaskA[2][GRASSHOPPER_FRAME_COUNT];
static tBitMap *s_pGrasshopper, *s_pGrasshopperMask, *s_pGrasshopperFlip, *s_pGrasshopperFlipMask;
static UBYTE *s_pWoodsFrame[2][WOODS_BOSS_FRAME_COUNT];
static UBYTE *s_pWoodsMaskA[2][WOODS_BOSS_FRAME_COUNT];
static tBitMap *s_pWoods, *s_pWoodsMask, *s_pWoodsFlip, *s_pWoodsFlipMask;
static tBitMap *s_pPiranhaBall, *s_pPiranhaBallMask, *s_pPiranhaBallFlip, *s_pPiranhaBallFlipMask;
static UBYTE *s_pPiranhaBallFrame[2][PIRANHA_BALL_FRAME_COUNT];
static UBYTE *s_pPiranhaBallMaskA[2][PIRANHA_BALL_FRAME_COUNT];
static tBitMap *s_pGooBm, *s_pGooMask;
static UBYTE *s_pGooFrame[GOO_FRAME_COUNT];
static UBYTE *s_pGooMaskA[GOO_FRAME_COUNT];

typedef struct {
	tBob sBob;
	LONG lPosX, lPosY, lVelX;
	WORD wDir;
	UBYTE isActive;
	UBYTE ubAnimFrame, ubAnimTimer;
} tPackShot;

static tPackShot s_pPackShots[PACK_SHOT_MAX];

typedef struct {
	tBob sBob;
	LONG lPosX, lPosY;
	UBYTE isActive;
	UWORD uwLife;
	UBYTE ubAnimFrame, ubAnimTimer;
} tGoo;

static tGoo s_pGoo[GOO_MAX];
static UBYTE s_ubHasWoodsBoss;
static UBYTE s_ubWalkFrames;
static UBYTE s_ubFlyFrames;
static UBYTE s_ubHopFrames;

// Per-facing, per-frame data/mask addresses ([FACE_LEFT|FACE_RIGHT][frame]).
// Sized for the winter cast (fox/owl/yeti); forest sheets use a prefix.
static UBYTE *s_pWalkerFrame[2][FOX_FRAME_COUNT];
static UBYTE *s_pWalkerMaskA[2][FOX_FRAME_COUNT];
static UBYTE *s_pFlyerFrame[2][OWL_FRAME_COUNT];
static UBYTE *s_pFlyerMaskA[2][OWL_FRAME_COUNT];
static UBYTE *s_pHopperIdleFrame[2][YETI_FRAME_COUNT];
static UBYTE *s_pHopperIdleMaskA[2][YETI_FRAME_COUNT];
static UBYTE *s_pHopperJumpFrame[2][YETI_FRAME_COUNT];
static UBYTE *s_pHopperJumpMaskA[2][YETI_FRAME_COUNT];

static tMonster s_pMonsters[LEVEL_MAX_ENEMIES];
static UWORD s_uwKilled;
static UWORD s_uwEnemyCount;
static UBYTE s_ubHalfFrame; // 0/1 frame-phase counter for the every-other-frame test

static inline UBYTE faceIdx(const tMonster *pMon) {
	return (pMon->wDir > 0) ? FACE_RIGHT : FACE_LEFT;
}

//------------------------------------------------------------- COLLISION HELPERS

static UBYTE solidAt(WORD wX, WORD wY) {
	if(colIsWalkTop(colAt(wX, wY))) {
		return 1;
	}
	return colHasFloor(wX, wY);
}

static void stickToGroundMon(tMonster *pMon) {
	WORD wLeft = TO_PX(pMon->lPosX) + pMon->ubHbX;
	WORD wRight = wLeft + pMon->ubHbW - 1;
	WORD wFeet = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH - 1;
	WORD wSurf;
	if(!colStickFeet(wLeft, wRight, wFeet, &wSurf)) {
		return;
	}
	WORD wNewTopPx = (wSurf - 1) - (pMon->ubHbY + pMon->ubHbH - 1);
	if(wNewTopPx != TO_PX(pMon->lPosY)) {
		pMon->lPosY = PX(wNewTopPx);
	}
	pMon->lVelY = 0;
}

//------------------------------------------------------------------- MOVEMENT

static void applyGravity(tMonster *pMon) {
	pMon->lVelY += MON_GRAVITY;
	if(pMon->lVelY > MON_MAX_FALL) {
		pMon->lVelY = MON_MAX_FALL;
	}
}

// Integrate vertical motion with tile collision. Returns 1 if we landed on
// something this frame (downward motion stopped by a solid/slope surface).
static UBYTE moveVertical(tMonster *pMon) {
	WORD wLeft = TO_PX(pMon->lPosX) + pMon->ubHbX;
	WORD wRight = wLeft + pMon->ubHbW - 1;
	WORD wOldBottom = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH - 1;
	LONG lNewY = pMon->lPosY + pMon->lVelY;
	WORD wNewTopPx = TO_PX(lNewY);
	UBYTE isLand = 0;

	if(pMon->lVelY > 0) {
		WORD wBottom = wNewTopPx + pMon->ubHbY + pMon->ubHbH - 1;
		WORD wHit = colSweepFloor(wOldBottom, wBottom, wLeft, wRight);
		if(wHit < 0) {
			WORD wPlatformTop = wBottom & ~(TILE_SIZE - 1);
			if(wOldBottom < wPlatformTop && colOneWayHSpan(wBottom, wLeft, wRight)) {
				wHit = wPlatformTop;
			}
		}
		if(wHit >= 0) {
			wNewTopPx = (wHit - 1) - (pMon->ubHbY + pMon->ubHbH - 1);
			lNewY = PX(wNewTopPx);
			pMon->lVelY = 0;
			isLand = 1;
		}
		{
			WORD wDoorHit = doorsTryLand(wOldBottom, wBottom, wLeft, wRight);
			if(wDoorHit >= 0 && (!isLand || wDoorHit <= wHit)) {
				wNewTopPx = (wDoorHit - 1) - (pMon->ubHbY + pMon->ubHbH - 1);
				lNewY = PX(wNewTopPx);
				pMon->lVelY = 0;
				isLand = 1;
			}
		}
	}
	else if(pMon->lVelY < 0) {
		WORD wHead = wNewTopPx + pMon->ubHbY;
		if(colBlockedCeiling(wHead, wLeft, wRight)) {
			WORD wSnapHead = (wHead & ~(TILE_SIZE - 1)) + TILE_SIZE;
			wNewTopPx = wSnapHead - pMon->ubHbY;
			lNewY = PX(wNewTopPx);
			pMon->lVelY = 0;
		}
		{
			WORD wDoorHead = doorsSnapCeiling(wHead, wLeft, wRight);
			if(wDoorHead >= 0) {
				wNewTopPx = wDoorHead - pMon->ubHbY;
				lNewY = PX(wNewTopPx);
				pMon->lVelY = 0;
			}
		}
	}
	pMon->lPosY = lNewY;
	return isLand;
}

// Integrate horizontal motion with tile collision. Reverses wDir on a wall.
// When isStick is set, snaps onto nearby slopes after the step (ground walkers).
static void moveHorizontal(tMonster *pMon, UBYTE isStick) {
	UBYTE isMovedX = (pMon->lVelX != 0);
	if(isMovedX) {
		LONG lNewX = pMon->lPosX + pMon->lVelX;
		WORD wNewLeftPx = TO_PX(lNewX);
		WORD wTop = TO_PX(pMon->lPosY) + pMon->ubHbY;
		WORD wBottom = wTop + pMon->ubHbH - 1;
		UBYTE isBlocked = 0;

		if(pMon->lVelX > 0) {
			WORD wRight = wNewLeftPx + pMon->ubHbX + pMon->ubHbW - 1;
			if(colBlockedVSpan(wRight, wTop, wBottom)) {
				WORD wSnapRight = (wRight & ~(TILE_SIZE - 1)) - 1;
				wNewLeftPx = wSnapRight - (pMon->ubHbX + pMon->ubHbW - 1);
				lNewX = PX(wNewLeftPx);
				isBlocked = 1;
			}
			{
				WORD wEdge = doorsSnapX(wRight, wTop, wBottom, 1);
				if(wEdge >= 0) {
					wNewLeftPx = wEdge - (pMon->ubHbX + pMon->ubHbW - 1);
					lNewX = PX(wNewLeftPx);
					isBlocked = 1;
				}
			}
		}
		else {
			WORD wLeft = wNewLeftPx + pMon->ubHbX;
			if(colBlockedVSpan(wLeft, wTop, wBottom)) {
				WORD wSnapLeft = (wLeft & ~(TILE_SIZE - 1)) + TILE_SIZE;
				wNewLeftPx = wSnapLeft - pMon->ubHbX;
				lNewX = PX(wNewLeftPx);
				isBlocked = 1;
			}
			{
				WORD wEdge = doorsSnapX(wLeft, wTop, wBottom, 0);
				if(wEdge >= 0) {
					wNewLeftPx = wEdge - pMon->ubHbX;
					lNewX = PX(wNewLeftPx);
					isBlocked = 1;
				}
			}
		}
		if(wNewLeftPx < 0) {
			wNewLeftPx = 0;
			lNewX = 0;
		}
		pMon->lPosX = lNewX;
		if(isBlocked) {
			pMon->wDir = -pMon->wDir;
			pMon->lVelX = 0;
		}
	}

	if(isStick) {
		stickToGroundMon(pMon);
	}
}

// Is a solid/slope surface directly under the feet right now?
static UBYTE probeGround(tMonster *pMon) {
	WORD wLeft = TO_PX(pMon->lPosX) + pMon->ubHbX;
	WORD wRight = wLeft + pMon->ubHbW - 1;
	WORD wBelow = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH;
	if((wBelow & (TILE_SIZE - 1)) == 0) {
		// Flat tile top under either foot — grounded without any scan.
		UBYTE ubL = colAt(wLeft, wBelow);
		UBYTE ubR = colAt(wRight, wBelow);
		if(ubL == COL_SOLID || ubR == COL_SOLID) {
			return 1;
		}
		if(pMon->lVelY >= 0 && (colIsWalkTop(ubL) || colIsWalkTop(ubR))) {
			return 1;
		}
	}
	// Hitboxes run up to 30px, so this must sweep the middle column too.
	return (UBYTE)(colFloorHitY(wBelow, wLeft, wRight) >= 0);
}

// Is there ground just beyond the leading foot? Used to stop ground walkers
// from strolling off ledges.
static UBYTE groundAhead(tMonster *pMon) {
	WORD wBelow = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH;
	WORD wAheadX;
	if(pMon->wDir > 0) {
		wAheadX = TO_PX(pMon->lPosX) + pMon->ubHbX + pMon->ubHbW - 1 + 2;
	}
	else {
		wAheadX = TO_PX(pMon->lPosX) + pMon->ubHbX - 2;
	}
	return solidAt(wAheadX, wBelow);
}

// True when the enemy has walked/flown past the outer edge of its spawn zone
// in the direction it is currently facing. Shared by every enemy type.
static UBYTE pastPatrolEdge(const tMonster *pMon) {
	if(pMon->wDir > 0) {
		return (UBYTE)(pMon->lPosX - pMon->lHomeX > MON_PATROL_RANGE);
	}
	return (UBYTE)(pMon->lHomeX - pMon->lPosX > MON_PATROL_RANGE);
}

//---------------------------------------------------------------- ANIMATION

// Only touch the bob's frame pointers when they actually change. bobSetFrame
// itself is cheap, but skipping the ~80% of ticks where the frame is identical
// keeps the per-enemy update loop tight (and matches the pickups approach).
static void setFrameIfChanged(tMonster *pMon, UBYTE *pFrame, UBYTE *pMask) {
	if(pMon->sBob.pFrameData != pFrame) {
		bobSetFrame(&pMon->sBob, pFrame, pMask);
	}
}

// Advance a looping clip and apply the current (facing-selected) frame.
static void animateCycle(
	tMonster *pMon, UBYTE ubFrameCount, UBYTE ubDelay,
	UBYTE **pFrames, UBYTE **pMasks
) {
	if(++pMon->ubAnimTimer >= ubDelay) {
		pMon->ubAnimTimer = 0;
		if(++pMon->ubAnimFrame >= ubFrameCount) {
			pMon->ubAnimFrame = 0;
		}
	}
	else if(pMon->ubAnimFrame >= ubFrameCount) {
		pMon->ubAnimFrame = 0; // guard when switching to a shorter clip
	}
	setFrameIfChanged(
		pMon, pFrames[pMon->ubAnimFrame], pMasks[pMon->ubAnimFrame]
	);
}

//------------------------------------------------------------------- PER-TYPE AI

static void processWalker(tMonster *pMon) {
	applyGravity(pMon);
	moveVertical(pMon);

	// Decide direction from terrain / zone, then walk. The ledge turn needs a
	// real ground probe: an airborne walker must keep falling, not turn.
	if(pastPatrolEdge(pMon) || (probeGround(pMon) && !groundAhead(pMon))) {
		pMon->wDir = -pMon->wDir; // zone edge or ledge ahead — turn back
	}
	pMon->lVelX = (pMon->wDir > 0) ? WALKER_SPEED : -WALKER_SPEED;
	moveHorizontal(pMon, 1);

	UBYTE f = faceIdx(pMon);
	animateCycle(
		pMon, s_ubWalkFrames, WALKER_ANIM_DELAY,
		s_pWalkerFrame[f], s_pWalkerMaskA[f]
	);
}

static void processFlyer(tMonster *pMon) {
	// Stay in the spawn-centred patrol zone; walls still reverse via moveHorizontal.
	if(pastPatrolEdge(pMon)) {
		pMon->wDir = -pMon->wDir;
	}
	pMon->lVelX = (pMon->wDir > 0) ? FLYER_SPEED : -FLYER_SPEED;
	moveHorizontal(pMon, 0);

	// Gentle vertical bob around the spawn height (triangle wave).
	pMon->lPosY += pMon->lVelY;
	if(pMon->lPosY - pMon->lHomeY > FLYER_VBOB_RANGE) {
		pMon->lVelY = -FLYER_VBOB_SPEED;
	}
	else if(pMon->lHomeY - pMon->lPosY > FLYER_VBOB_RANGE) {
		pMon->lVelY = FLYER_VBOB_SPEED;
	}

	UBYTE f = faceIdx(pMon);
	animateCycle(
		pMon, s_ubFlyFrames, FLYER_ANIM_DELAY,
		s_pFlyerFrame[f], s_pFlyerMaskA[f]
	);
}

static void processHopper(tMonster *pMon) {
	applyGravity(pMon);
	moveVertical(pMon);
	UBYTE isGround = probeGround(pMon);

	if(isGround) {
		pMon->lVelX = 0; // settle between hops
		if(++pMon->uwTimer >= HOPPER_HOP_PERIOD) {
			pMon->uwTimer = 0;
			// Turn around at a ledge or zone edge before committing to the next hop.
			if(pastPatrolEdge(pMon) || !groundAhead(pMon)) {
				pMon->wDir = -pMon->wDir;
			}
			pMon->lVelY = -HOPPER_HOP_VY;
			pMon->lVelX = (pMon->wDir > 0) ? HOPPER_HOP_VX : -HOPPER_HOP_VX;
		}
	}
	// Stick to slopes only while grounded and not rising into a hop.
	moveHorizontal(pMon, (UBYTE)(isGround && pMon->lVelY >= 0));

	UBYTE f = faceIdx(pMon);
	if(s_ubWinterSet) {
		animateCycle(
			pMon, s_ubHopFrames, HOPPER_ANIM_DELAY,
			s_pHopperIdleFrame[f], s_pHopperIdleMaskA[f]
		);
	}
	else if(isGround) {
		animateCycle(
			pMon, s_ubHopFrames, HOPPER_ANIM_DELAY,
			s_pHopperIdleFrame[f], s_pHopperIdleMaskA[f]
		);
	}
	else {
		// Airborne: rising uses the launch frame, falling the last frame.
		UBYTE ubSub = (pMon->lVelY < 0) ? 1 : (FROG_JUMP_FRAME_COUNT - 1);
		setFrameIfChanged(
			pMon, s_pHopperJumpFrame[f][ubSub], s_pHopperJumpMaskA[f][ubSub]
		);
	}
}

static UBYTE isBossType(UBYTE ubType) {
	return (UBYTE)(ubType == ENEMY_TYPE_BOSS || ubType == ENEMY_TYPE_BOSS_WOODS);
}

static void bossShotSpawn(tMonster *pMon);

static tBitMap *loadLevelSprite(const char *szName) {
	char szPath[LEVEL_PATH_MAX];
	levelSpritePath(szPath, szName);
	return sunnyLoadBitmap(szPath, 0);
}

static void loadPackSheet(
	tPackSheet *pS, const char *szBm, const char *szMask,
	const char *szFlip, const char *szFlipMask,
	UBYTE ubCount, UWORD uwFrameH, UBYTE ubSrcRight
) {
	pS->pBm = loadLevelSprite(szBm);
	pS->pMask = loadLevelSprite(szMask);
	pS->pFlip = loadLevelSprite(szFlip);
	pS->pFlipMask = loadLevelSprite(szFlipMask);
	pS->ubCount = ubCount;
	for(UBYTE i = 0; i < ubCount && i < PACK_FRAME_MAX; ++i) {
		UWORD o = i * uwFrameH;
		if(ubSrcRight) {
			pS->pFrame[FACE_RIGHT][i] = bobCalcFrameAddress(pS->pBm, o);
			pS->pMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(pS->pMask, o);
			pS->pFrame[FACE_LEFT][i] = bobCalcFrameAddress(pS->pFlip, o);
			pS->pMaskA[FACE_LEFT][i] = bobCalcFrameAddress(pS->pFlipMask, o);
		}
		else {
			pS->pFrame[FACE_LEFT][i] = bobCalcFrameAddress(pS->pBm, o);
			pS->pMaskA[FACE_LEFT][i] = bobCalcFrameAddress(pS->pMask, o);
			pS->pFrame[FACE_RIGHT][i] = bobCalcFrameAddress(pS->pFlip, o);
			pS->pMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(pS->pFlipMask, o);
		}
	}
}

static void destroyPackSheet(tPackSheet *pS) {
	bitmapDestroy(pS->pBm);
	bitmapDestroy(pS->pMask);
	bitmapDestroy(pS->pFlip);
	bitmapDestroy(pS->pFlipMask);
	pS->pBm = 0;
	pS->pMask = 0;
	pS->pFlip = 0;
	pS->pFlipMask = 0;
	pS->ubCount = 0;
}

static void animatePack(tMonster *pMon, tPackSheet *pS, UBYTE ubDelay) {
	UBYTE f = faceIdx(pMon);
	animateCycle(pMon, pS->ubCount, ubDelay, pS->pFrame[f], pS->pMaskA[f]);
}

static void processWalkerSheet(tMonster *pMon, tPackSheet *pS, LONG lSpeed, UBYTE ubDelay) {
	applyGravity(pMon);
	moveVertical(pMon);
	if(pastPatrolEdge(pMon) || (probeGround(pMon) && !groundAhead(pMon))) {
		pMon->wDir = -pMon->wDir;
	}
	pMon->lVelX = (pMon->wDir > 0) ? lSpeed : -lSpeed;
	moveHorizontal(pMon, 1);
	animatePack(pMon, pS, ubDelay);
}

static void processChaser(tMonster *pMon, tPackSheet *pS, LONG lWalk, LONG lRun) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wMx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	WORD wDx = (wPX + (WORD)(uwPW >> 1)) - wMx;
	LONG lSpeed = lWalk;
	if(wDx < 0) {
		wDx = (WORD)-wDx;
	}
	if(wDx < TO_PX(CHASER_RANGE)) {
		pMon->wDir = ((wPX + (WORD)(uwPW >> 1)) >= wMx) ? 1 : -1;
		lSpeed = lRun;
	}
	applyGravity(pMon);
	moveVertical(pMon);
	if(pastPatrolEdge(pMon) || (probeGround(pMon) && !groundAhead(pMon))) {
		// Chasing into a ledge or the patrol limit used to flip every update.
		if(lSpeed == lRun) {
			pMon->lVelX = 0;
		}
		else {
			pMon->wDir = -pMon->wDir;
			pMon->lVelX = (pMon->wDir > 0) ? lSpeed : -lSpeed;
		}
	}
	else {
		pMon->lVelX = (pMon->wDir > 0) ? lSpeed : -lSpeed;
	}
	moveHorizontal(pMon, 1);
	animatePack(pMon, pS, WALKER_ANIM_DELAY);
}

static void processCharger(tMonster *pMon, tPackSheet *pS) {
	applyGravity(pMon);
	moveVertical(pMon);
	if(pMon->ubAction == 0) {
		if(pastPatrolEdge(pMon) || (probeGround(pMon) && !groundAhead(pMon))) {
			pMon->wDir = -pMon->wDir;
		}
		pMon->lVelX = (pMon->wDir > 0) ? CHARGE_WALK : -CHARGE_WALK;
		if(++pMon->uwTimer >= CHARGE_REST) {
			pMon->uwTimer = 0;
			pMon->ubAction = 1;
		}
	}
	else if(pMon->ubAction == 1) {
		pMon->lVelX = 0;
		if(++pMon->uwTimer >= CHARGE_TELEGRAPH) {
			pMon->uwTimer = 0;
			pMon->ubAction = 2;
		}
	}
	else {
		pMon->lVelX = (pMon->wDir > 0) ? CHARGE_BURST : -CHARGE_BURST;
		if(pastPatrolEdge(pMon) || (probeGround(pMon) && !groundAhead(pMon))) {
			pMon->wDir = -pMon->wDir;
			pMon->ubAction = 0;
			pMon->uwTimer = 0;
		}
		else if(++pMon->uwTimer >= CHARGE_GO) {
			pMon->uwTimer = 0;
			pMon->ubAction = 0;
		}
	}
	moveHorizontal(pMon, 1);
	animatePack(pMon, pS, (UBYTE)((pMon->ubAction == 2) ? 2 : WALKER_ANIM_DELAY));
}

static void processSwooper(tMonster *pMon, tPackSheet *pS, UBYTE ubShallow) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wMx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	WORD wUnder = (WORD)((wPX <= wMx && wPX + (WORD)uwPW >= wMx) ? 1 : 0);

	if(pMon->ubAction == 0) {
		if(pastPatrolEdge(pMon)) {
			pMon->wDir = -pMon->wDir;
		}
		pMon->lVelX = (pMon->wDir > 0) ? FLYER_SPEED : -FLYER_SPEED;
		moveHorizontal(pMon, 0);
		pMon->lPosY += pMon->lVelY;
		if(pMon->lPosY - pMon->lHomeY > FLYER_VBOB_RANGE) {
			pMon->lVelY = -FLYER_VBOB_SPEED;
		}
		else if(pMon->lHomeY - pMon->lPosY > FLYER_VBOB_RANGE) {
			pMon->lVelY = FLYER_VBOB_SPEED;
		}
		if(wUnder && (wPY > TO_PX(pMon->lPosY))) {
			pMon->ubAction = 1;
			pMon->lVelY = SWOOP_DIVE;
		}
	}
	else if(pMon->ubAction == 1) {
		pMon->lVelX = (pMon->wDir > 0) ? FLYER_SPEED : -FLYER_SPEED;
		moveHorizontal(pMon, 0);
		pMon->lPosY += pMon->lVelY;
		if(pMon->lPosY - pMon->lHomeY > (ubShallow ? PX(28) : PX(48))) {
			pMon->ubAction = 2;
			pMon->lVelY = -SWOOP_CLIMB;
		}
	}
	else {
		pMon->lVelX = (pMon->wDir > 0) ? FLYER_SPEED : -FLYER_SPEED;
		moveHorizontal(pMon, 0);
		pMon->lPosY += pMon->lVelY;
		if(pMon->lPosY <= pMon->lHomeY) {
			pMon->lPosY = pMon->lHomeY;
			pMon->ubAction = 0;
			pMon->lVelY = FLYER_VBOB_SPEED;
		}
	}
	animatePack(pMon, pS, FLYER_ANIM_DELAY);
}

static void processLeaper(tMonster *pMon) {
	applyGravity(pMon);
	moveVertical(pMon);
	UBYTE isGround = probeGround(pMon);
	if(isGround) {
		pMon->lVelX = 0;
		if(++pMon->uwTimer >= HOPPER_HOP_PERIOD) {
			pMon->uwTimer = 0;
			WORD wPX, wPY;
			UWORD uwPW, uwPH;
			playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
			WORD wMx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
			if((wPX + (WORD)(uwPW >> 1)) >= wMx) {
				pMon->wDir = 1;
			}
			else {
				pMon->wDir = -1;
			}
			if(pastPatrolEdge(pMon) || !groundAhead(pMon)) {
				pMon->wDir = -pMon->wDir;
			}
			pMon->lVelY = -LEAPER_HOP_VY;
			pMon->lVelX = (pMon->wDir > 0) ? LEAPER_HOP_VX : -LEAPER_HOP_VX;
		}
	}
	moveHorizontal(pMon, (UBYTE)(isGround && pMon->lVelY >= 0));
	UBYTE f = faceIdx(pMon);
	if(isGround) {
		UWORD uwFirst = s_grasshopper_Anim[0][0];
		UWORD uwCount = s_grasshopper_Anim[0][1];
		if(++pMon->ubAnimTimer >= HOPPER_ANIM_DELAY) {
			pMon->ubAnimTimer = 0;
			if(++pMon->ubAnimFrame >= uwCount) {
				pMon->ubAnimFrame = 0;
			}
		}
		UWORD uwFrame = uwFirst + pMon->ubAnimFrame;
		setFrameIfChanged(
			pMon, s_pHopperPackFrame[f][uwFrame], s_pHopperPackMaskA[f][uwFrame]
		);
	}
	else {
		UBYTE ubRow = (pMon->lVelY < 0) ? 1 : 2;
		UWORD uwFrame = s_grasshopper_Anim[ubRow][0];
		setFrameIfChanged(
			pMon, s_pHopperPackFrame[f][uwFrame], s_pHopperPackMaskA[f][uwFrame]
		);
	}
}

static void packShotSpawn(tMonster *pMon, LONG lVx) {
	for(UBYTE i = 0; i < PACK_SHOT_MAX; ++i) {
		tPackShot *pShot = &s_pPackShots[i];
		if(pShot->isActive) {
			continue;
		}
		WORD wX = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
		WORD wY = TO_PX(pMon->lPosY) + pMon->ubHbY + 4;
		pShot->lPosX = PX(wX);
		pShot->lPosY = PX(wY);
		pShot->lVelX = (pMon->wDir > 0) ? lVx : -lVx;
		pShot->wDir = pMon->wDir;
		pShot->isActive = 1;
		pShot->ubAnimFrame = 0;
		pShot->ubAnimTimer = 0;
		sfxPlaySpit();
		return;
	}
}

static void packShotsProcess(void) {
	if(!s_pPiranhaBall) {
		return;
	}
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;
	for(UBYTE i = 0; i < PACK_SHOT_MAX; ++i) {
		tPackShot *pShot = &s_pPackShots[i];
		if(!pShot->isActive) {
			continue;
		}
		pShot->lPosX += pShot->lVelX;
		WORD wX = TO_PX(pShot->lPosX);
		WORD wY = TO_PX(pShot->lPosY);
		if(wX < -PIRANHA_BALL_FRAME_W || wX > (WORD)g_uwColWorldWPx) {
			pShot->isActive = 0;
			continue;
		}
		if(colIsBlocking(colAt(wX + 8, wY + 8))) {
			pShot->isActive = 0;
			continue;
		}
		if(wX <= wPR && wX + PIRANHA_BALL_FRAME_W_ART >= wPX
			&& wY <= wPB && wY + PIRANHA_BALL_FRAME_H >= wPY) {
			playerHurt(wX + 8);
			pShot->isActive = 0;
			continue;
		}
		if(++pShot->ubAnimTimer >= 4) {
			pShot->ubAnimTimer = 0;
			if(++pShot->ubAnimFrame >= PIRANHA_BALL_FRAME_COUNT) {
				pShot->ubAnimFrame = 0;
			}
		}
		UBYTE f = (pShot->wDir > 0) ? FACE_RIGHT : FACE_LEFT;
		bobSetFrame(
			&pShot->sBob,
			s_pPiranhaBallFrame[f][pShot->ubAnimFrame],
			s_pPiranhaBallMaskA[f][pShot->ubAnimFrame]
		);
		pShot->sBob.sPos.uwX = (UWORD)wX;
		pShot->sBob.sPos.uwY = (UWORD)wY;
	}
}

static void processShooter(tMonster *pMon) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wMx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	pMon->wDir = ((wPX + (WORD)(uwPW >> 1)) >= wMx) ? 1 : -1;
	pMon->lVelX = 0;
	pMon->lVelY = 0;
	UBYTE f = faceIdx(pMon);
	if(++pMon->uwTimer >= SHOOTER_PERIOD) {
		pMon->uwTimer = 0;
		pMon->ubAction = 1;
		pMon->ubAnimFrame = 0;
		packShotSpawn(pMon, SHOOTER_BALL_VX);
	}
	if(pMon->ubAction) {
		animateCycle(
			pMon, PIRANHA_ATTACK_FRAME_COUNT, 4,
			s_sPiranhaAtk.pFrame[f], s_sPiranhaAtk.pMaskA[f]
		);
		if(pMon->ubAnimFrame == 0 && pMon->uwTimer > 4) {
			pMon->ubAction = 0;
		}
	}
	else {
		animatePack(pMon, &s_sPiranha, 8);
	}
}

static void gooSpawn(tMonster *pMon) {
	if(!s_pGooBm) {
		return;
	}
	for(UBYTE i = 0; i < GOO_MAX; ++i) {
		tGoo *pGoo = &s_pGoo[i];
		if(pGoo->isActive) {
			continue;
		}
		WORD wX = TO_PX(pMon->lPosX) + pMon->ubHbX;
		WORD wY = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH - GOO_FRAME_H;
		pGoo->lPosX = PX(wX);
		pGoo->lPosY = PX(wY);
		pGoo->isActive = 1;
		pGoo->uwLife = GOO_LIFE;
		pGoo->ubAnimFrame = 0;
		pGoo->ubAnimTimer = 0;
		return;
	}
}

static void gooProcess(void) {
	if(!s_pGooBm) {
		return;
	}
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;
	for(UBYTE i = 0; i < GOO_MAX; ++i) {
		tGoo *pGoo = &s_pGoo[i];
		if(!pGoo->isActive) {
			continue;
		}
		if(pGoo->uwLife) {
			--pGoo->uwLife;
		}
		else {
			pGoo->isActive = 0;
			continue;
		}
		WORD wX = TO_PX(pGoo->lPosX);
		WORD wY = TO_PX(pGoo->lPosY);
		if(wX <= wPR && wX + GOO_FRAME_W_ART >= wPX
			&& wY <= wPB && wY + GOO_FRAME_H >= wPY) {
			playerHurt(wX + GOO_FRAME_W_ART / 2);
		}
		if(++pGoo->ubAnimTimer >= 6) {
			pGoo->ubAnimTimer = 0;
			if(++pGoo->ubAnimFrame >= GOO_FRAME_COUNT) {
				pGoo->ubAnimFrame = 0;
			}
		}
		bobSetFrame(&pGoo->sBob, s_pGooFrame[pGoo->ubAnimFrame], s_pGooMaskA[pGoo->ubAnimFrame]);
		pGoo->sBob.sPos.uwX = (UWORD)wX;
		pGoo->sBob.sPos.uwY = (UWORD)wY;
	}
}

static void processCrawler(tMonster *pMon) {
	processWalkerSheet(pMon, &s_sSlimer, SLIMER_SPEED, 7);
	if(++pMon->uwTimer >= GOO_PERIOD) {
		pMon->uwTimer = 0;
		gooSpawn(pMon);
	}
}

static void woodsSetAnim(tMonster *pMon, UBYTE ubAnim) {
	UBYTE f = faceIdx(pMon);
	UWORD uwFirst = s_woods_boss_Anim[ubAnim][0];
	UWORD uwCount = s_woods_boss_Anim[ubAnim][1];
	if(pMon->ubAnimFrame >= uwCount) {
		pMon->ubAnimFrame = 0;
	}
	UWORD uwFrame = uwFirst + pMon->ubAnimFrame;
	setFrameIfChanged(
		pMon, s_pWoodsFrame[f][uwFrame], s_pWoodsMaskA[f][uwFrame]
	);
}

static void woodsAnimate(tMonster *pMon, UBYTE ubAnim, UBYTE ubDelay) {
	UWORD uwCount = s_woods_boss_Anim[ubAnim][1];
	if(++pMon->ubAnimTimer >= ubDelay) {
		pMon->ubAnimTimer = 0;
		if(++pMon->ubAnimFrame >= uwCount) {
			pMon->ubAnimFrame = 0;
		}
	}
	woodsSetAnim(pMon, ubAnim);
}

static void processWoodsBoss(tMonster *pMon) {
	if(pMon->ubInvuln) {
		--pMon->ubInvuln;
	}
	applyGravity(pMon);
	UBYTE isLand = moveVertical(pMon);
	UBYTE isGround = probeGround(pMon);
	if(isLand) {
		isGround = 1;
	}

	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wBossX = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	WORD wPlayerCx = wPX + (WORD)(uwPW >> 1);
	if(pMon->ubAction != BOSS_ACT_THROW) {
		pMon->wDir = (wPlayerCx >= wBossX) ? 1 : -1;
	}

	if(pMon->ubAction == BOSS_ACT_THROW) {
		pMon->lVelX = 0;
		moveHorizontal(pMon, isGround);
		woodsAnimate(pMon, WOODS_ANIM_THROW, 6);
		if(pMon->ubAnimFrame == 1 && pMon->ubAnimTimer == 0) {
			bossShotSpawn(pMon);
		}
		if(pMon->ubAnimFrame == 0 && pMon->uwTimer > 8) {
			pMon->ubAction = BOSS_ACT_NONE;
			pMon->uwTimer = 0;
		}
		++pMon->uwTimer;
		return;
	}

	if(isGround) {
		++pMon->uwTimer;
		++pMon->uwHopTimer;
		UWORD uwThrow = (pMon->ubHp <= 3) ? (BOSS_THROW_PERIOD / 2) : BOSS_THROW_PERIOD;
		if(pMon->uwTimer >= uwThrow) {
			pMon->uwTimer = 0;
			pMon->ubAction = BOSS_ACT_THROW;
			pMon->ubAnimFrame = 0;
			pMon->ubAnimTimer = 0;
			pMon->lVelX = 0;
			woodsSetAnim(pMon, WOODS_ANIM_THROW);
			return;
		}
		if(groundAhead(pMon)) {
			pMon->lVelX = (pMon->wDir > 0) ? BOSS_SPEED : -BOSS_SPEED;
		}
		else {
			pMon->lVelX = 0;
		}
		if(pMon->uwHopTimer >= BOSS_HOP_PERIOD && groundAhead(pMon)) {
			pMon->uwHopTimer = 0;
			pMon->lVelY = -BOSS_HOP_VY;
			pMon->lVelX = (pMon->wDir > 0) ? BOSS_SPEED : -BOSS_SPEED;
		}
	}
	moveHorizontal(pMon, (UBYTE)(isGround && pMon->lVelY >= 0));

	if(pMon->ubInvuln) {
		woodsAnimate(pMon, WOODS_ANIM_HURT, 4);
	}
	else if(!isGround) {
		pMon->ubAnimFrame = (pMon->lVelY < 0) ? 1 : (s_woods_boss_Anim[WOODS_ANIM_JUMP][1] - 1);
		pMon->ubAnimTimer = 0;
		woodsSetAnim(pMon, WOODS_ANIM_JUMP);
	}
	else if(pMon->lVelX) {
		woodsAnimate(pMon, WOODS_ANIM_RUN, 4);
	}
	else {
		woodsAnimate(pMon, WOODS_ANIM_IDLE, 8);
	}
}

static void processPackType(tMonster *pMon) {
	switch(pMon->ubType) {
		case ENEMY_TYPE_BEETLE:
			processWalkerSheet(pMon, &s_sBeetle, BEETLE_SPEED, 6);
			break;
		case ENEMY_TYPE_SLUG:
			processWalkerSheet(pMon, &s_sSlug, SLUG_SPEED, 8);
			break;
		case ENEMY_TYPE_SLIMER:
			processCrawler(pMon);
			break;
		case ENEMY_TYPE_DOG:
			processChaser(pMon, &s_sDog, CHASER_WALK, CHASER_RUN);
			break;
		case ENEMY_TYPE_ANT:
			processChaser(pMon, &s_sAnt, CHASER_WALK, CHASER_RUN);
			break;
		case ENEMY_TYPE_DINO:
			processCharger(pMon, &s_sDino);
			break;
		case ENEMY_TYPE_GATOR:
			processCharger(pMon, &s_sGator);
			break;
		case ENEMY_TYPE_VULTURE:
			processSwooper(pMon, &s_sVulture, 0);
			break;
		case ENEMY_TYPE_BEE:
			processSwooper(pMon, &s_sBee, 1);
			break;
		case ENEMY_TYPE_GRASSHOPPER:
			processLeaper(pMon);
			break;
		case ENEMY_TYPE_PIRANHA:
			processShooter(pMon);
			break;
		case ENEMY_TYPE_BOSS_WOODS:
			processWoodsBoss(pMon);
			break;
		default:
			break;
	}
}

static void bossSetAnim(tMonster *pMon, UBYTE ubAnim) {
	UBYTE f = faceIdx(pMon);
	UWORD uwFirst = s_boss_Anim[ubAnim][0];
	UWORD uwCount = s_boss_Anim[ubAnim][1];
	if(pMon->ubAnimFrame >= uwCount) {
		pMon->ubAnimFrame = 0;
	}
	UWORD uwFrame = uwFirst + pMon->ubAnimFrame;
	setFrameIfChanged(
		pMon, s_pBossFrame[f][uwFrame], s_pBossMaskA[f][uwFrame]
	);
}

static void bossAnimate(tMonster *pMon, UBYTE ubAnim, UBYTE ubDelay) {
	UWORD uwCount = s_boss_Anim[ubAnim][1];
	if(++pMon->ubAnimTimer >= ubDelay) {
		pMon->ubAnimTimer = 0;
		if(++pMon->ubAnimFrame >= uwCount) {
			pMon->ubAnimFrame = 0;
		}
	}
	bossSetAnim(pMon, ubAnim);
}

static UBYTE bossOnScreen(const tMonster *pMon) {
	if(!s_pTileBuffer || !s_pTileBuffer->pCamera) {
		return 0;
	}
	WORD wCamX = (WORD)s_pTileBuffer->pCamera->uPos.uwX;
	WORD wCamY = (WORD)s_pTileBuffer->pCamera->uPos.uwY;
	WORD wX = TO_PX(pMon->lPosX);
	WORD wY = TO_PX(pMon->lPosY);
	WORD wR = wX + (WORD)pMon->sBob.uwWidth - 1;
	WORD wB = wY + (WORD)pMon->sBob.uwHeight - 1;
	if(wR < wCamX || wB < wCamY) {
		return 0;
	}
	if(wX >= wCamX + SCREEN_W || wY >= wCamY + SCREEN_H) {
		return 0;
	}
	return 1;
}

static void bossShotSpawn(tMonster *pMon) {
	for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
		tBossShot *pShot = &s_pBossShots[i];
		if(pShot->isActive) {
			continue;
		}
		WORD wX = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
		WORD wY = TO_PX(pMon->lPosY) + pMon->ubHbY + 8;
		if(pMon->wDir > 0) {
			wX += 12;
		}
		else {
			wX -= 12;
		}
		pShot->lPosX = PX(wX);
		pShot->lPosY = PX(wY);
		pShot->lVelX = (pMon->wDir > 0) ? BOSS_SHOT_VX : -BOSS_SHOT_VX;
		pShot->lVelY = BOSS_SHOT_VY;
		pShot->wDir = pMon->wDir;
		pShot->isActive = 1;
		pShot->ubAnimFrame = 0;
		pShot->ubAnimTimer = 0;
		if(bossOnScreen(pMon)) {
			sfxPlaySpit();
		}
		return;
	}
}

static void bossShotsProcess(void) {
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;

	for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
		tBossShot *pShot = &s_pBossShots[i];
		if(!pShot->isActive) {
			continue;
		}
		pShot->lVelY += BOSS_SHOT_GRAVITY;
		pShot->lPosX += pShot->lVelX;
		pShot->lPosY += pShot->lVelY;
		WORD wX = TO_PX(pShot->lPosX);
		WORD wY = TO_PX(pShot->lPosY);
		if(wY > (WORD)g_uwColWorldHPx || wX < -(WORD)s_uwShotW
			|| wX > (WORD)g_uwColWorldWPx) {
			pShot->isActive = 0;
			continue;
		}
		WORD wCx = wX + (WORD)s_uwShotArtW / 2;
		WORD wCy = wY + (WORD)s_uwShotH / 2;
		if(colIsBlocking(colAt(wCx, wCy))) {
			pShot->isActive = 0;
			continue;
		}
		if(doorsHitWorldRect(
			wX, wY, wX + (WORD)s_uwShotArtW - 1, wY + (WORD)s_uwShotH - 1
		)) {
			pShot->isActive = 0;
			continue;
		}
		if(wX <= wPR && wX + (WORD)s_uwShotArtW >= wPX
			&& wY <= wPB && wY + (WORD)s_uwShotH >= wPY) {
			playerHurt(wCx);
			pShot->isActive = 0;
			continue;
		}
		if(++pShot->ubAnimTimer >= 4) {
			pShot->ubAnimTimer = 0;
			if(++pShot->ubAnimFrame >= s_ubShotFrames) {
				pShot->ubAnimFrame = 0;
			}
		}
		UBYTE f = (pShot->wDir > 0) ? FACE_RIGHT : FACE_LEFT;
		bobSetFrame(
			&pShot->sBob,
			s_pBossShotFrame[f][pShot->ubAnimFrame],
			s_pBossShotMaskA[f][pShot->ubAnimFrame]
		);
		pShot->sBob.sPos.uwX = (UWORD)wX;
		pShot->sBob.sPos.uwY = (UWORD)wY;
	}
}

static void processBoss(tMonster *pMon) {
	if(pMon->ubInvuln) {
		--pMon->ubInvuln;
	}
	applyGravity(pMon);
	UBYTE isLand = moveVertical(pMon);
	UBYTE isGround = probeGround(pMon);
	if(isLand) {
		isGround = 1;
	}

	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wBossX = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	WORD wPlayerCx = wPX + (WORD)(uwPW >> 1);
	if(pMon->ubAction != BOSS_ACT_THROW) {
		pMon->wDir = (wPlayerCx >= wBossX) ? 1 : -1;
	}

	if(pMon->ubAction == BOSS_ACT_THROW) {
		pMon->lVelX = 0;
		moveHorizontal(pMon, isGround);
		bossAnimate(pMon, BOSS_ANIM_SHOT, 5);
		if(pMon->ubAnimFrame == 2 && pMon->ubAnimTimer == 0) {
			bossShotSpawn(pMon);
		}
		if(pMon->ubAnimFrame == 0 && pMon->uwTimer > 8) {
			pMon->ubAction = BOSS_ACT_NONE;
			pMon->uwTimer = 0;
		}
		++pMon->uwTimer;
		return;
	}

	if(isGround) {
		++pMon->uwTimer;
		++pMon->uwHopTimer;
		UWORD uwThrow = (pMon->ubHp <= 2) ? (BOSS_THROW_PERIOD / 2) : BOSS_THROW_PERIOD;
		if(pMon->uwTimer >= uwThrow) {
			pMon->uwTimer = 0;
			pMon->ubAction = BOSS_ACT_THROW;
			pMon->ubAnimFrame = 0;
			pMon->ubAnimTimer = 0;
			pMon->lVelX = 0;
			bossSetAnim(pMon, BOSS_ANIM_SHOT);
			return;
		}
		WORD wPlayerFeet = wPY + (WORD)uwPH - 1;
		WORD wBossFeet = TO_PX(pMon->lPosY) + pMon->ubHbY + pMon->ubHbH - 1;
		UBYTE isPlayerAbove = (UBYTE)(wPlayerFeet + 8 < wBossFeet);
		if(pMon->uwHopTimer >= BOSS_HOP_PERIOD && (groundAhead(pMon) || isPlayerAbove)) {
			pMon->uwHopTimer = 0;
			pMon->lVelY = -BOSS_HOP_VY;
			pMon->lVelX = (pMon->wDir > 0) ? BOSS_SPEED : -BOSS_SPEED;
		}
		else if(groundAhead(pMon)) {
			pMon->lVelX = (pMon->wDir > 0) ? BOSS_SPEED : -BOSS_SPEED;
		}
		else {
			pMon->lVelX = 0;
		}
	}
	moveHorizontal(pMon, (UBYTE)(isGround && pMon->lVelY >= 0));

	if(pMon->ubInvuln) {
		bossAnimate(pMon, BOSS_ANIM_HURT, 4);
	}
	else if(!isGround) {
		pMon->ubAnimFrame = (pMon->lVelY < 0) ? 1 : (s_boss_Anim[BOSS_ANIM_JUMP][1] - 1);
		pMon->ubAnimTimer = 0;
		bossSetAnim(pMon, BOSS_ANIM_JUMP);
	}
	else if(pMon->lVelX) {
		bossAnimate(pMon, BOSS_ANIM_RUN, 4);
	}
	else {
		bossAnimate(pMon, BOSS_ANIM_IDLE, 8);
	}
}

static void monsterKill(tMonster *pMon);

static void monsterDamage(tMonster *pMon) {
	if(!isBossType(pMon->ubType)) {
		if(pMon->ubInvuln) {
			return;
		}
		if(pMon->ubHp > 1) {
			--pMon->ubHp;
			pMon->ubInvuln = 12;
			return;
		}
		monsterKill(pMon);
		return;
	}
	if(pMon->ubInvuln) {
		return;
	}
	if(pMon->ubHp) {
		--pMon->ubHp;
	}
	s_ubBossHp = pMon->ubHp;
	pMon->ubInvuln = BOSS_INVULN_FRAMES;
	pMon->ubAnimFrame = 0;
	pMon->ubAnimTimer = 0;
	if(!pMon->ubHp) {
		monsterKill(pMon);
	}
}

// Kill the enemy, play the death poof, and maybe drop a heart.
static void monsterKill(tMonster *pMon) {
	WORD wCx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
	WORD wCy = TO_PX(pMon->lPosY) + pMon->ubHbY + (pMon->ubHbH >> 1);
	pMon->isActive = 0;
	++s_uwKilled;
	effectsSpawnDeath(wCx, wCy);
	if(isBossType(pMon->ubType)) {
		s_ubBossAlive = 0;
		s_ubBossHp = 0;
		for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
			s_pBossShots[i].isActive = 0;
		}
		pickupsSpawnDroppedGems(wCx, wCy);
	}
	else {
		pickupsTryDropHeart(wCx, wCy);
	}
	sfxPlayMonsterDie();
}

// Resolve player-vs-enemy contact for one enemy, given the player's world
// hitbox (already fetched this frame) and whether the player is descending.
// A stomp (falling onto the enemy's upper half) damages it and bounces the
// player; side contact hurts the player.
static void collidePlayer(
	tMonster *pMon, WORD wPX, WORD wPY, WORD wPR, WORD wPB, UBYTE ubFalling
) {
	WORD wEL = TO_PX(pMon->lPosX) + pMon->ubHbX;
	WORD wET = TO_PX(pMon->lPosY) + pMon->ubHbY;
	WORD wER = wEL + pMon->ubHbW - 1;
	WORD wEB = wET + pMon->ubHbH - 1;

	if(wPX > wER || wPR < wEL || wPY > wEB || wPB < wET) {
		return; // no overlap
	}
	// Stomp: descending and the player's feet are in the enemy's upper half.
	// An enemy still in its post-stomp invulnerability window does not hurt
	// the player; a second falling contact only bounces.
	if(ubFalling && wPB <= wET + (pMon->ubHbH >> 1)) {
		if(!pMon->ubInvuln) {
			monsterDamage(pMon);
		}
		playerBounce();
	}
	else if(!pMon->ubInvuln) {
		// Side/underside contact: the player takes a hit (knockback away from
		// the enemy's centre). Harmless while the player is invulnerable.
		playerHurt(wEL + (pMon->ubHbW >> 1));
	}
}

//------------------------------------------------------------------- PUBLIC

static UBYTE levelHasEnemyType(UBYTE ubType) {
	UWORD uwN = levelGetEnemyCount();
	const tLevelEntity *pEnemies = levelGetEnemies();
	for(UWORD i = 0; i < uwN; ++i) {
		if(pEnemies[i].ubType == ubType) {
			return 1;
		}
	}
	return 0;
}

static void freeBossShots(void) {
	if(s_pBossShot) {
		bitmapDestroy(s_pBossShot);
		s_pBossShot = 0;
	}
	if(s_pBossShotMask) {
		bitmapDestroy(s_pBossShotMask);
		s_pBossShotMask = 0;
	}
	if(s_pBossShotFlip) {
		bitmapDestroy(s_pBossShotFlip);
		s_pBossShotFlip = 0;
	}
	if(s_pBossShotFlipMask) {
		bitmapDestroy(s_pBossShotFlipMask);
		s_pBossShotFlipMask = 0;
	}
}

void monstersLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;
	s_uwKilled = 0;
	s_ubHasBoss = levelHasEnemyType(ENEMY_TYPE_BOSS);
	s_ubHasWoodsBoss = levelHasEnemyType(ENEMY_TYPE_BOSS_WOODS);
	s_ubBossAlive = 0;
	s_ubBossHp = 0;
	s_ubBossHpMax = 0;
	s_ubWinterSet = levelIsWinter();
	s_uwShotW = BOSS_SHOT_FRAME_W;
	s_uwShotH = BOSS_SHOT_FRAME_H;
	s_uwShotArtW = BOSS_SHOT_FRAME_W_ART;
	s_ubShotFrames = BOSS_SHOT_FRAME_COUNT;
	s_ubWalkFrames = s_ubWinterSet ? FOX_FRAME_COUNT : OPOSUM_FRAME_COUNT;
	s_ubFlyFrames = s_ubWinterSet ? OWL_FRAME_COUNT : EAGLE_ATTACK_FRAME_COUNT;
	s_ubHopFrames = s_ubWinterSet ? YETI_FRAME_COUNT : FROG_IDLE_FRAME_COUNT;

	s_pWalker = 0; s_pWalkerMask = 0; s_pWalkerFlip = 0; s_pWalkerFlipMask = 0;
	s_pFlyer = 0; s_pFlyerMask = 0; s_pFlyerFlip = 0; s_pFlyerFlipMask = 0;
	s_pHopperIdle = 0; s_pHopperIdleMask = 0; s_pHopperIdleFlip = 0; s_pHopperIdleFlipMask = 0;
	s_pHopperJump = 0; s_pHopperJumpMask = 0; s_pHopperJumpFlip = 0; s_pHopperJumpFlipMask = 0;
	s_pBoss = 0; s_pBossMask = 0; s_pBossFlip = 0; s_pBossFlipMask = 0;
	s_pBossShot = 0; s_pBossShotMask = 0; s_pBossShotFlip = 0; s_pBossShotFlipMask = 0;
	s_pWoods = 0; s_pWoodsMask = 0; s_pWoodsFlip = 0; s_pWoodsFlipMask = 0;
	s_pGrasshopper = 0; s_pGrasshopperMask = 0;
	s_pGrasshopperFlip = 0; s_pGrasshopperFlipMask = 0;
	s_pPiranhaBall = 0; s_pPiranhaBallMask = 0;
	s_pPiranhaBallFlip = 0; s_pPiranhaBallFlipMask = 0;
	s_pGooBm = 0; s_pGooMask = 0;

	UBYTE hasWalker = levelHasEnemyType(ENEMY_TYPE_WALKER);
	UBYTE hasFlyer = levelHasEnemyType(ENEMY_TYPE_FLYER);
	UBYTE hasHopper = levelHasEnemyType(ENEMY_TYPE_HOPPER);
	if(s_ubWinterSet) {
		if(hasWalker) {
			s_pWalker = sunnyLoadBitmap("data/sprites/fox.bm", 0);
			s_pWalkerMask = sunnyLoadBitmap("data/sprites/fox_mask.bm", 0);
			s_pWalkerFlip = sunnyLoadBitmap("data/sprites/fox_flip.bm", 0);
			s_pWalkerFlipMask = sunnyLoadBitmap("data/sprites/fox_flip_mask.bm", 0);
			// Winter fox art faces RIGHT.
			for(UBYTE i = 0; i < FOX_FRAME_COUNT; ++i) {
				UWORD o = i * FOX_FRAME_H;
				s_pWalkerFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWalker, o);
				s_pWalkerMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWalkerMask, o);
				s_pWalkerFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pWalkerFlip, o);
				s_pWalkerMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pWalkerFlipMask, o);
			}
		}
		if(hasFlyer) {
			s_pFlyer = sunnyLoadBitmap("data/sprites/owl.bm", 0);
			s_pFlyerMask = sunnyLoadBitmap("data/sprites/owl_mask.bm", 0);
			s_pFlyerFlip = sunnyLoadBitmap("data/sprites/owl_flip.bm", 0);
			s_pFlyerFlipMask = sunnyLoadBitmap("data/sprites/owl_flip_mask.bm", 0);
			// Owl source faces LEFT.
			for(UBYTE i = 0; i < OWL_FRAME_COUNT; ++i) {
				UWORD o = i * OWL_FRAME_H;
				s_pFlyerFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pFlyer, o);
				s_pFlyerMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pFlyerMask, o);
				s_pFlyerFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pFlyerFlip, o);
				s_pFlyerMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pFlyerFlipMask, o);
			}
		}
		if(hasHopper) {
			s_pHopperIdle = sunnyLoadBitmap("data/sprites/yeti.bm", 0);
			s_pHopperIdleMask = sunnyLoadBitmap("data/sprites/yeti_mask.bm", 0);
			s_pHopperIdleFlip = sunnyLoadBitmap("data/sprites/yeti_flip.bm", 0);
			s_pHopperIdleFlipMask = sunnyLoadBitmap("data/sprites/yeti_flip_mask.bm", 0);
			// Winter yeti art faces RIGHT.
			for(UBYTE i = 0; i < YETI_FRAME_COUNT; ++i) {
				UWORD o = i * YETI_FRAME_H;
				s_pHopperIdleFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperIdle, o);
				s_pHopperIdleMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperIdleMask, o);
				s_pHopperIdleFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pHopperIdleFlip, o);
				s_pHopperIdleMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pHopperIdleFlipMask, o);
			}
		}
	}
	else {
		if(hasWalker) {
			s_pWalker = sunnyLoadBitmap("data/sprites/oposum.bm", 0);
			s_pWalkerMask = sunnyLoadBitmap("data/sprites/oposum_mask.bm", 0);
			s_pWalkerFlip = sunnyLoadBitmap("data/sprites/oposum_flip.bm", 0);
			s_pWalkerFlipMask = sunnyLoadBitmap("data/sprites/oposum_flip_mask.bm", 0);
			// Opossum art faces LEFT.
			for(UBYTE i = 0; i < OPOSUM_FRAME_COUNT; ++i) {
				UWORD o = i * OPOSUM_FRAME_H;
				s_pWalkerFrame[FACE_LEFT][i]  = bobCalcFrameAddress(s_pWalker, o);
				s_pWalkerMaskA[FACE_LEFT][i]  = bobCalcFrameAddress(s_pWalkerMask, o);
				s_pWalkerFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWalkerFlip, o);
				s_pWalkerMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWalkerFlipMask, o);
			}
		}
		if(hasFlyer) {
			s_pFlyer = sunnyLoadBitmap("data/sprites/eagle_attack.bm", 0);
			s_pFlyerMask = sunnyLoadBitmap("data/sprites/eagle_attack_mask.bm", 0);
			s_pFlyerFlip = sunnyLoadBitmap("data/sprites/eagle_attack_flip.bm", 0);
			s_pFlyerFlipMask = sunnyLoadBitmap("data/sprites/eagle_attack_flip_mask.bm", 0);
			// Eagle art faces LEFT.
			for(UBYTE i = 0; i < EAGLE_ATTACK_FRAME_COUNT; ++i) {
				UWORD o = i * EAGLE_ATTACK_FRAME_H;
				s_pFlyerFrame[FACE_LEFT][i]  = bobCalcFrameAddress(s_pFlyer, o);
				s_pFlyerMaskA[FACE_LEFT][i]  = bobCalcFrameAddress(s_pFlyerMask, o);
				s_pFlyerFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pFlyerFlip, o);
				s_pFlyerMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pFlyerFlipMask, o);
			}
		}
		if(hasHopper) {
			s_pHopperIdle = sunnyLoadBitmap("data/sprites/frog_idle.bm", 0);
			s_pHopperIdleMask = sunnyLoadBitmap("data/sprites/frog_idle_mask.bm", 0);
			s_pHopperIdleFlip = sunnyLoadBitmap("data/sprites/frog_idle_flip.bm", 0);
			s_pHopperIdleFlipMask = sunnyLoadBitmap("data/sprites/frog_idle_flip_mask.bm", 0);

			s_pHopperJump = sunnyLoadBitmap("data/sprites/frog_jump.bm", 0);
			s_pHopperJumpMask = sunnyLoadBitmap("data/sprites/frog_jump_mask.bm", 0);
			s_pHopperJumpFlip = sunnyLoadBitmap("data/sprites/frog_jump_flip.bm", 0);
			s_pHopperJumpFlipMask = sunnyLoadBitmap("data/sprites/frog_jump_flip_mask.bm", 0);

			// Frog art faces LEFT.
			for(UBYTE i = 0; i < FROG_IDLE_FRAME_COUNT; ++i) {
				UWORD o = i * FROG_IDLE_FRAME_H;
				s_pHopperIdleFrame[FACE_LEFT][i]  = bobCalcFrameAddress(s_pHopperIdle, o);
				s_pHopperIdleMaskA[FACE_LEFT][i]  = bobCalcFrameAddress(s_pHopperIdleMask, o);
				s_pHopperIdleFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperIdleFlip, o);
				s_pHopperIdleMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperIdleFlipMask, o);
			}
			for(UBYTE i = 0; i < FROG_JUMP_FRAME_COUNT; ++i) {
				UWORD o = i * FROG_JUMP_FRAME_H;
				s_pHopperJumpFrame[FACE_LEFT][i]  = bobCalcFrameAddress(s_pHopperJump, o);
				s_pHopperJumpMaskA[FACE_LEFT][i]  = bobCalcFrameAddress(s_pHopperJumpMask, o);
				s_pHopperJumpFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperJumpFlip, o);
				s_pHopperJumpMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pHopperJumpFlipMask, o);
			}
		}
	}

	if(s_ubHasBoss) {
		freeBossShots();
		s_pBoss = sunnyLoadBitmap("data/sprites/boss.bm", 0);
		s_pBossMask = sunnyLoadBitmap("data/sprites/boss_mask.bm", 0);
		s_pBossFlip = sunnyLoadBitmap("data/sprites/boss_flip.bm", 0);
		s_pBossFlipMask = sunnyLoadBitmap("data/sprites/boss_flip_mask.bm", 0);
		s_pBossShot = sunnyLoadBitmap("data/sprites/boss_shot.bm", 0);
		s_pBossShotMask = sunnyLoadBitmap("data/sprites/boss_shot_mask.bm", 0);
		s_pBossShotFlip = sunnyLoadBitmap("data/sprites/boss_shot_flip.bm", 0);
		s_pBossShotFlipMask = sunnyLoadBitmap("data/sprites/boss_shot_flip_mask.bm", 0);

		// Winter rival art is drawn facing RIGHT, so the base strip is
		// right-facing and the _flip strip is left-facing.
		for(UBYTE i = 0; i < BOSS_FRAME_COUNT; ++i) {
			UWORD o = i * BOSS_FRAME_H;
			s_pBossFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBoss, o);
			s_pBossMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBossMask, o);
			s_pBossFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossFlip, o);
			s_pBossMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossFlipMask, o);
		}
		for(UBYTE i = 0; i < BOSS_SHOT_FRAME_COUNT; ++i) {
			UWORD o = i * BOSS_SHOT_FRAME_H;
			s_pBossShotFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBossShot, o);
			s_pBossShotMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBossShotMask, o);
			s_pBossShotFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossShotFlip, o);
			s_pBossShotMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossShotFlipMask, o);
		}
		s_ubBossHpMax = BOSS_HP_MAX;
	}

	if(s_ubHasWoodsBoss) {
		freeBossShots();
		s_pWoods = loadLevelSprite("woods_boss.bm");
		s_pWoodsMask = loadLevelSprite("woods_boss_mask.bm");
		s_pWoodsFlip = loadLevelSprite("woods_boss_flip.bm");
		s_pWoodsFlipMask = loadLevelSprite("woods_boss_flip_mask.bm");
		s_pBossShot = loadLevelSprite("woods_boss_shot.bm");
		s_pBossShotMask = loadLevelSprite("woods_boss_shot_mask.bm");
		s_pBossShotFlip = loadLevelSprite("woods_boss_shot_flip.bm");
		s_pBossShotFlipMask = loadLevelSprite("woods_boss_shot_flip_mask.bm");
		s_uwShotW = WOODS_BOSS_SHOT_FRAME_W;
		s_uwShotH = WOODS_BOSS_SHOT_FRAME_H;
		s_uwShotArtW = WOODS_BOSS_SHOT_FRAME_W_ART;
		s_ubShotFrames = WOODS_BOSS_SHOT_FRAME_COUNT;
		for(UBYTE i = 0; i < WOODS_BOSS_FRAME_COUNT; ++i) {
			UWORD o = i * WOODS_BOSS_FRAME_H;
			s_pWoodsFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWoods, o);
			s_pWoodsMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pWoodsMask, o);
			s_pWoodsFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pWoodsFlip, o);
			s_pWoodsMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pWoodsFlipMask, o);
		}
		for(UBYTE i = 0; i < WOODS_BOSS_SHOT_FRAME_COUNT; ++i) {
			UWORD o = i * WOODS_BOSS_SHOT_FRAME_H;
			s_pBossShotFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBossShot, o);
			s_pBossShotMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pBossShotMask, o);
			s_pBossShotFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossShotFlip, o);
			s_pBossShotMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pBossShotFlipMask, o);
		}
		s_ubHasBoss = 1;
		s_ubBossHpMax = WOODS_BOSS_HP_MAX;
	}

	if(levelHasEnemyType(ENEMY_TYPE_BEETLE)) {
		loadPackSheet(
			&s_sBeetle,
			"beetle.bm", "beetle_mask.bm",
			"beetle_flip.bm", "beetle_flip_mask.bm",
			BEETLE_FRAME_COUNT, BEETLE_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_SLUG)) {
		loadPackSheet(
			&s_sSlug,
			"slug.bm", "slug_mask.bm",
			"slug_rev.bm", "slug_rev_mask.bm",
			SLUG_FRAME_COUNT, SLUG_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_SLIMER)) {
		loadPackSheet(
			&s_sSlimer,
			"slimer.bm", "slimer_mask.bm",
			"slimer_flip.bm", "slimer_flip_mask.bm",
			SLIMER_FRAME_COUNT, SLIMER_FRAME_H, 0
		);
		s_pGooBm = loadLevelSprite("goo.bm");
		s_pGooMask = loadLevelSprite("goo_mask.bm");
		for(UBYTE i = 0; i < GOO_FRAME_COUNT; ++i) {
			s_pGooFrame[i] = bobCalcFrameAddress(s_pGooBm, i * GOO_FRAME_H);
			s_pGooMaskA[i] = bobCalcFrameAddress(s_pGooMask, i * GOO_FRAME_H);
		}
	}
	if(levelHasEnemyType(ENEMY_TYPE_ANT) || levelHasEnemyType(ENEMY_TYPE_DOG)) {
		loadPackSheet(
			&s_sAnt,
			"ant.bm", "ant_mask.bm",
			"ant_rev.bm", "ant_rev_mask.bm",
			ANT_FRAME_COUNT, ANT_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_DINO)) {
		loadPackSheet(
			&s_sDino,
			"dino.bm", "dino_mask.bm",
			"dino_flip.bm", "dino_flip_mask.bm",
			DINO_FRAME_COUNT, DINO_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_GATOR)) {
		loadPackSheet(
			&s_sGator,
			"gator.bm", "gator_mask.bm",
			"gator_flip.bm", "gator_flip_mask.bm",
			GATOR_FRAME_COUNT, GATOR_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_VULTURE)) {
		loadPackSheet(
			&s_sVulture,
			"vulture.bm", "vulture_mask.bm",
			"vulture_flip.bm", "vulture_flip_mask.bm",
			VULTURE_FRAME_COUNT, VULTURE_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_BEE)) {
		loadPackSheet(
			&s_sBee,
			"bee.bm", "bee_mask.bm",
			"bee_flip.bm", "bee_flip_mask.bm",
			BEE_FRAME_COUNT, BEE_FRAME_H, 0
		);
	}
	if(levelHasEnemyType(ENEMY_TYPE_GRASSHOPPER)) {
		s_pGrasshopper = loadLevelSprite("grasshopper.bm");
		s_pGrasshopperMask = loadLevelSprite("grasshopper_mask.bm");
		s_pGrasshopperFlip = loadLevelSprite("grasshopper_flip.bm");
		s_pGrasshopperFlipMask = loadLevelSprite("grasshopper_flip_mask.bm");
		for(UBYTE i = 0; i < GRASSHOPPER_FRAME_COUNT; ++i) {
			UWORD o = i * GRASSHOPPER_FRAME_H;
			s_pHopperPackFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pGrasshopper, o);
			s_pHopperPackMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pGrasshopperMask, o);
			s_pHopperPackFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pGrasshopperFlip, o);
			s_pHopperPackMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pGrasshopperFlipMask, o);
		}
	}
	if(levelHasEnemyType(ENEMY_TYPE_PIRANHA)) {
		loadPackSheet(
			&s_sPiranha,
			"piranha.bm", "piranha_mask.bm",
			"piranha_flip.bm", "piranha_flip_mask.bm",
			PIRANHA_FRAME_COUNT, PIRANHA_FRAME_H, 0
		);
		loadPackSheet(
			&s_sPiranhaAtk,
			"piranha_attack.bm", "piranha_attack_mask.bm",
			"piranha_attack_flip.bm", "piranha_attack_flip_mask.bm",
			PIRANHA_ATTACK_FRAME_COUNT, PIRANHA_ATTACK_FRAME_H, 0
		);
		s_pPiranhaBall = loadLevelSprite("piranha_ball.bm");
		s_pPiranhaBallMask = loadLevelSprite("piranha_ball_mask.bm");
		s_pPiranhaBallFlip = loadLevelSprite("piranha_ball_flip.bm");
		s_pPiranhaBallFlipMask = loadLevelSprite("piranha_ball_flip_mask.bm");
		for(UBYTE i = 0; i < PIRANHA_BALL_FRAME_COUNT; ++i) {
			UWORD o = i * PIRANHA_BALL_FRAME_H;
			s_pPiranhaBallFrame[FACE_LEFT][i] = bobCalcFrameAddress(s_pPiranhaBall, o);
			s_pPiranhaBallMaskA[FACE_LEFT][i] = bobCalcFrameAddress(s_pPiranhaBallMask, o);
			s_pPiranhaBallFrame[FACE_RIGHT][i] = bobCalcFrameAddress(s_pPiranhaBallFlip, o);
			s_pPiranhaBallMaskA[FACE_RIGHT][i] = bobCalcFrameAddress(s_pPiranhaBallFlipMask, o);
		}
	}
}

// Fill gameplay state and the bob's first-frame placement. uwFrameW/H are
// only needed when registering the bob for the first time.
static void monsterSpawn(
	tMonster *pMon, const tLevelEntity *pDef, UWORD i,
	UWORD *puwFrameW, UWORD *puwFrameH, UBYTE **ppFrame, UBYTE **ppMask,
	WORD *pwBobX, WORD *pwBobY
) {
	pMon->ubType = pDef->ubType;
	if(pMon->ubType == ENEMY_TYPE_DOG) {
		pMon->ubType = ENEMY_TYPE_ANT;
	}
	pMon->isActive = 1;
	pMon->ubAnimFrame = 0;
	pMon->ubAnimTimer = 0;
	pMon->uwTimer = 0;
	pMon->uwHopTimer = 0;
	pMon->isAwake = 0;
	pMon->lVelX = 0;
	pMon->lVelY = 0;
	pMon->ubHp = 1;
	pMon->ubInvuln = 0;
	pMon->ubAction = 0;
	// Alternate initial facing so a cluster of enemies doesn't march in lockstep.
	pMon->wDir = (i & 1) ? 1 : -1;
	if(isBossType(pMon->ubType)) {
		pMon->wDir = -1;
	}
	UBYTE f = faceIdx(pMon);

	UWORD uwFrameW, uwFrameH, uwArtW;
	UBYTE *pFrame, *pMask;
	pMon->ubDrawRaise = 0;
	switch(pMon->ubType) {
		case ENEMY_TYPE_BOSS:
			uwFrameW = BOSS_FRAME_W; uwFrameH = BOSS_FRAME_H;
			uwArtW = BOSS_FRAME_W_ART;
			pFrame = s_pBossFrame[f][0]; pMask = s_pBossMaskA[f][0];
			pMon->ubHbX = 10; pMon->ubHbW = 28; pMon->ubHbY = 12; pMon->ubHbH = 28;
			pMon->ubHp = BOSS_HP_MAX;
			s_ubBossAlive = 1;
			s_ubBossHp = BOSS_HP_MAX;
			break;
		case ENEMY_TYPE_BOSS_WOODS:
			uwFrameW = WOODS_BOSS_FRAME_W; uwFrameH = WOODS_BOSS_FRAME_H;
			uwArtW = WOODS_BOSS_FRAME_W_ART;
			pFrame = s_pWoodsFrame[f][0]; pMask = s_pWoodsMaskA[f][0];
			pMon->ubHbX = 16; pMon->ubHbW = 48; pMon->ubHbY = 18; pMon->ubHbH = 36;
			pMon->ubHp = WOODS_BOSS_HP_MAX;
			s_ubBossAlive = 1;
			s_ubBossHp = WOODS_BOSS_HP_MAX;
			break;
		case ENEMY_TYPE_BEETLE:
			uwFrameW = BEETLE_FRAME_W; uwFrameH = BEETLE_FRAME_H;
			uwArtW = BEETLE_FRAME_W_ART;
			pFrame = s_sBeetle.pFrame[f][0]; pMask = s_sBeetle.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 28; pMon->ubHbY = 10; pMon->ubHbH = 26;
			pMon->ubHp = 2;
			break;
		case ENEMY_TYPE_SLUG:
			uwFrameW = SLUG_FRAME_W; uwFrameH = SLUG_FRAME_H;
			uwArtW = SLUG_FRAME_W_ART;
			pFrame = s_sSlug.pFrame[f][0]; pMask = s_sSlug.pMaskA[f][0];
			pMon->ubHbX = 2; pMon->ubHbW = 28; pMon->ubHbY = 4; pMon->ubHbH = 16;
			pMon->ubHp = 2;
			break;
		case ENEMY_TYPE_SLIMER:
			uwFrameW = SLIMER_FRAME_W; uwFrameH = SLIMER_FRAME_H;
			uwArtW = SLIMER_FRAME_W_ART;
			pFrame = s_sSlimer.pFrame[f][0]; pMask = s_sSlimer.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 32; pMon->ubHbY = 8; pMon->ubHbH = 28;
			break;
		case ENEMY_TYPE_DOG:
			uwFrameW = DOG_FRAME_W; uwFrameH = DOG_FRAME_H;
			uwArtW = DOG_FRAME_W_ART;
			pFrame = s_sDog.pFrame[f][0]; pMask = s_sDog.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 26; pMon->ubHbY = 4; pMon->ubHbH = 20;
			break;
		case ENEMY_TYPE_ANT:
			uwFrameW = ANT_FRAME_W; uwFrameH = ANT_FRAME_H;
			uwArtW = ANT_FRAME_W_ART;
			pFrame = s_sAnt.pFrame[f][0]; pMask = s_sAnt.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 28; pMon->ubHbY = 6; pMon->ubHbH = 22;
			break;
		case ENEMY_TYPE_DINO:
			uwFrameW = DINO_FRAME_W; uwFrameH = DINO_FRAME_H;
			uwArtW = DINO_FRAME_W_ART;
			pFrame = s_sDino.pFrame[f][0]; pMask = s_sDino.pMaskA[f][0];
			pMon->ubHbX = 2; pMon->ubHbW = 28; pMon->ubHbY = 4; pMon->ubHbH = 20;
			break;
		case ENEMY_TYPE_GATOR:
			uwFrameW = GATOR_FRAME_W; uwFrameH = GATOR_FRAME_H;
			uwArtW = GATOR_FRAME_W_ART;
			pFrame = s_sGator.pFrame[f][0]; pMask = s_sGator.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 38; pMon->ubHbY = 16; pMon->ubHbH = 30;
			break;
		case ENEMY_TYPE_VULTURE:
			uwFrameW = VULTURE_FRAME_W; uwFrameH = VULTURE_FRAME_H;
			uwArtW = VULTURE_FRAME_W_ART;
			pFrame = s_sVulture.pFrame[f][0]; pMask = s_sVulture.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 30; pMon->ubHbY = 6; pMon->ubHbH = 28;
			break;
		case ENEMY_TYPE_BEE:
			uwFrameW = BEE_FRAME_W; uwFrameH = BEE_FRAME_H;
			uwArtW = BEE_FRAME_W_ART;
			pFrame = s_sBee.pFrame[f][0]; pMask = s_sBee.pMaskA[f][0];
			pMon->ubHbX = 4; pMon->ubHbW = 28; pMon->ubHbY = 6; pMon->ubHbH = 28;
			break;
		case ENEMY_TYPE_GRASSHOPPER:
			uwFrameW = GRASSHOPPER_FRAME_W; uwFrameH = GRASSHOPPER_FRAME_H;
			uwArtW = GRASSHOPPER_FRAME_W_ART;
			pFrame = s_pHopperPackFrame[f][0]; pMask = s_pHopperPackMaskA[f][0];
			pMon->ubHbX = 8; pMon->ubHbW = 36; pMon->ubHbY = 10; pMon->ubHbH = 32;
			break;
		case ENEMY_TYPE_PIRANHA:
			uwFrameW = PIRANHA_FRAME_W; uwFrameH = PIRANHA_FRAME_H;
			uwArtW = PIRANHA_FRAME_W_ART;
			pFrame = s_sPiranha.pFrame[f][0]; pMask = s_sPiranha.pMaskA[f][0];
			pMon->ubHbX = 8; pMon->ubHbW = 44; pMon->ubHbY = 8; pMon->ubHbH = 34;
			break;
		case ENEMY_TYPE_FLYER:
			if(s_ubWinterSet) {
				uwFrameW = OWL_FRAME_W; uwFrameH = OWL_FRAME_H;
				uwArtW = OWL_FRAME_W_ART;
				pMon->ubHbX = 12; pMon->ubHbW = 36; pMon->ubHbY = 10; pMon->ubHbH = 36;
			}
			else {
				uwFrameW = EAGLE_ATTACK_FRAME_W; uwFrameH = EAGLE_ATTACK_FRAME_H;
				uwArtW = EAGLE_ATTACK_FRAME_W_ART;
				pMon->ubHbX = 5; pMon->ubHbW = 30; pMon->ubHbY = 8; pMon->ubHbH = 26;
			}
			pFrame = s_pFlyerFrame[f][0]; pMask = s_pFlyerMaskA[f][0];
			break;
		case ENEMY_TYPE_HOPPER:
			if(s_ubWinterSet) {
				uwFrameW = YETI_FRAME_W; uwFrameH = YETI_FRAME_H;
				uwArtW = YETI_FRAME_W_ART;
				pMon->ubHbX = 4; pMon->ubHbW = 24; pMon->ubHbY = 6; pMon->ubHbH = 24;
			}
			else {
				uwFrameW = FROG_IDLE_FRAME_W; uwFrameH = FROG_IDLE_FRAME_H;
				uwArtW = FROG_IDLE_FRAME_W_ART;
				pMon->ubHbX = 4; pMon->ubHbW = 27; pMon->ubHbY = 8; pMon->ubHbH = 24;
			}
			pFrame = s_pHopperIdleFrame[f][0]; pMask = s_pHopperIdleMaskA[f][0];
			break;
		case ENEMY_TYPE_WALKER:
		default:
			if(s_ubWinterSet) {
				uwFrameW = FOX_FRAME_W; uwFrameH = FOX_FRAME_H;
				uwArtW = FOX_FRAME_W_ART;
				pMon->ubHbX = 8; pMon->ubHbW = 44; pMon->ubHbY = 8; pMon->ubHbH = 26;
			}
			else {
				uwFrameW = OPOSUM_FRAME_W; uwFrameH = OPOSUM_FRAME_H;
				uwArtW = OPOSUM_FRAME_W_ART;
				pMon->ubHbX = 4; pMon->ubHbW = 28; pMon->ubHbY = 4; pMon->ubHbH = 24;
				// Opossum art is drawn with its feet almost flush with the
				// frame bottom (only ~1px clearance), so it renders sunk into
				// the ground next to the frog (5px). Lift it to match.
				pMon->ubDrawRaise = 4;
			}
			pFrame = s_pWalkerFrame[f][0]; pMask = s_pWalkerMaskA[f][0];
			break;
	}

	// Place the art centred over the spawn tile column, with the frame's
	// bottom aligned to the bottom of the spawn tile row (feet on the floor).
	WORD wCenterX = (WORD)(pDef->uwTileX << TILE_SHIFT) + (TILE_SIZE / 2);
	WORD wFloorY = (WORD)((pDef->uwTileY + 1) << TILE_SHIFT);
	WORD wX = wCenterX - (WORD)(uwArtW / 2);
	WORD wY = wFloorY - (WORD)uwFrameH;
	if(wX < 0) {
		wX = 0;
	}
	pMon->lPosX = PX(wX);
	pMon->lPosY = PX(wY);
	pMon->lHomeX = pMon->lPosX;
	pMon->lHomeY = pMon->lPosY;
	if(pMon->ubType == ENEMY_TYPE_FLYER
		|| pMon->ubType == ENEMY_TYPE_VULTURE
		|| pMon->ubType == ENEMY_TYPE_BEE) {
		pMon->lVelY = FLYER_VBOB_SPEED; // start bobbing downward
	}

	*puwFrameW = uwFrameW;
	*puwFrameH = uwFrameH;
	*ppFrame = pFrame;
	*ppMask = pMask;
	*pwBobX = wX;
	*pwBobY = wY - pMon->ubDrawRaise;
}

void monstersInitBobs(void) {
	s_uwEnemyCount = levelGetEnemyCount();
	if(s_uwEnemyCount > LEVEL_MAX_ENEMIES) {
		s_uwEnemyCount = LEVEL_MAX_ENEMIES;
	}
	const tLevelEntity *pEnemies = levelGetEnemies();
	for(UWORD i = 0; i < s_uwEnemyCount; ++i) {
		UWORD uwFrameW, uwFrameH;
		UBYTE *pFrame, *pMask;
		WORD wBobX, wBobY;
		monsterSpawn(
			&s_pMonsters[i], &pEnemies[i], i,
			&uwFrameW, &uwFrameH, &pFrame, &pMask, &wBobX, &wBobY
		);
		bobInit(
			&s_pMonsters[i].sBob, uwFrameW, uwFrameH, 1, pFrame, pMask,
			(UWORD)wBobX, (UWORD)wBobY
		);
	}
	for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
		s_pBossShots[i].isActive = 0;
		if(!s_pBossShot) {
			continue;
		}
		bobInit(
			&s_pBossShots[i].sBob, s_uwShotW, s_uwShotH, 1,
			s_pBossShotFrame[FACE_RIGHT][0], s_pBossShotMaskA[FACE_RIGHT][0],
			0, 0
		);
	}
	for(UBYTE i = 0; i < PACK_SHOT_MAX; ++i) {
		s_pPackShots[i].isActive = 0;
		if(!s_pPiranhaBall) {
			continue;
		}
		bobInit(
			&s_pPackShots[i].sBob, PIRANHA_BALL_FRAME_W, PIRANHA_BALL_FRAME_H, 1,
			s_pPiranhaBallFrame[FACE_RIGHT][0], s_pPiranhaBallMaskA[FACE_RIGHT][0],
			0, 0
		);
	}
	for(UBYTE i = 0; i < GOO_MAX; ++i) {
		s_pGoo[i].isActive = 0;
		if(!s_pGooBm) {
			continue;
		}
		bobInit(
			&s_pGoo[i].sBob, GOO_FRAME_W, GOO_FRAME_H, 1,
			s_pGooFrame[0], s_pGooMaskA[0], 0, 0
		);
	}
}

void monstersReset(void) {
	s_uwKilled = 0;
	s_ubHalfFrame = 0;
	const tLevelEntity *pEnemies = levelGetEnemies();
	for(UWORD i = 0; i < s_uwEnemyCount; ++i) {
		UWORD uwFrameW, uwFrameH;
		UBYTE *pFrame, *pMask;
		WORD wBobX, wBobY;
		tMonster *pMon = &s_pMonsters[i];
		monsterSpawn(
			pMon, &pEnemies[i], i,
			&uwFrameW, &uwFrameH, &pFrame, &pMask, &wBobX, &wBobY
		);
		(void)uwFrameW;
		(void)uwFrameH;
		bobSetFrame(&pMon->sBob, pFrame, pMask);
		pMon->sBob.sPos.uwX = (UWORD)wBobX;
		pMon->sBob.sPos.uwY = (UWORD)wBobY;
		pMon->sBob.pOldPositions[0] = pMon->sBob.sPos;
		pMon->sBob.pOldPositions[1] = pMon->sBob.sPos;
	}
	for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
		s_pBossShots[i].isActive = 0;
	}
	for(UBYTE i = 0; i < PACK_SHOT_MAX; ++i) {
		s_pPackShots[i].isActive = 0;
	}
	for(UBYTE i = 0; i < GOO_MAX; ++i) {
		s_pGoo[i].isActive = 0;
	}
}

// Shipped 68000 cost cut: regular enemy AI/collision runs every other frame.
// Bob pushing still runs each frame so motion appears at half rate. The boss
// always ticks so the finale stays at full rate.
#define MONSTERS_EVERY_OTHER_FRAME 1

void monstersProcess(void) {
	UBYTE isSkipRegular = 0;
	if(MONSTERS_EVERY_OTHER_FRAME) {
		s_ubHalfFrame ^= 1;
		isSkipRegular = (UBYTE)!s_ubHalfFrame;
	}

	UWORD uwCamX = s_pTileBuffer->pCamera->uPos.uwX;
	UWORD uwCamY = s_pTileBuffer->pCamera->uPos.uwY;
	WORD wActL = (WORD)uwCamX - PROCESS_MARGIN;
	WORD wActT = (WORD)uwCamY - PROCESS_MARGIN;
	WORD wActR = (WORD)uwCamX + SCREEN_W + PROCESS_MARGIN;
	WORD wActB = (WORD)uwCamY + SCREEN_H + PROCESS_MARGIN;

	// Player hitbox + descent state, fetched once for all stomp tests.
	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;
	UBYTE ubFalling = playerIsFalling();

	for(UWORD i = 0; i < s_uwEnemyCount; ++i) {
		tMonster *pMon = &s_pMonsters[i];
		if(!pMon->isActive) {
			continue;
		}
		UBYTE isBoss = isBossType(pMon->ubType);
		if(!isBoss && pMon->ubInvuln) {
			--pMon->ubInvuln;
		}
		if(!isBoss && isSkipRegular) {
			continue;
		}
		// Skip AI + collision for enemies far from the camera. This is the bulk
		// of the per-frame cost (each enemy scans tiles for ground/walls), and
		// only a handful are ever near the player in this wide level.
		// The boss always ticks so the finale does not run at half rate.
		if(!isBoss) {
			WORD wX = TO_PX(pMon->lPosX);
			WORD wY = TO_PX(pMon->lPosY);
			if(wX > wActR || wY > wActB
				|| (wX + (WORD)pMon->sBob.uwWidth) < wActL
				|| (wY + (WORD)pMon->sBob.uwHeight) < wActT) {
				continue;
			}
		}
		else if(!pMon->isAwake) {
			WORD wMx = TO_PX(pMon->lPosX) + pMon->ubHbX + (pMon->ubHbW >> 1);
			WORD wMy = TO_PX(pMon->lPosY) + pMon->ubHbY + (pMon->ubHbH >> 1);
			WORD wDx = wMx - (wPX + (WORD)(uwPW >> 1));
			WORD wDy = wMy - (wPY + (WORD)(uwPH >> 1));
			if(wDx < 0) {
				wDx = (WORD)-wDx;
			}
			if(wDy < 0) {
				wDy = (WORD)-wDy;
			}
			if(wDx <= SCREEN_W && wDy <= SCREEN_H) {
				pMon->isAwake = 1;
			}
			else {
				continue;
			}
		}
		switch(pMon->ubType) {
			case ENEMY_TYPE_BOSS:    processBoss(pMon);    break;
			case ENEMY_TYPE_FLYER:   processFlyer(pMon);   break;
			case ENEMY_TYPE_HOPPER:    processHopper(pMon);    break;
			case ENEMY_TYPE_WALKER:  processWalker(pMon); break;
			default:                 processPackType(pMon); break;
		}
		collidePlayer(pMon, wPX, wPY, wPR, wPB, ubFalling);
	}
	if(s_pBossShot) {
		bossShotsProcess();
	}
	packShotsProcess();
	gooProcess();
}

void monstersPushBobs(void) {
	UWORD uwCamX = s_pTileBuffer->pCamera->uPos.uwX;
	UWORD uwCamY = s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewL = (WORD)uwCamX - CULL_MARGIN;
	WORD wViewT = (WORD)uwCamY - CULL_MARGIN;
	WORD wViewR = (WORD)uwCamX + SCREEN_W + CULL_MARGIN;
	WORD wViewB = (WORD)uwCamY + SCREEN_H + CULL_MARGIN;

	for(UWORD i = 0; i < s_uwEnemyCount; ++i) {
		tMonster *pMon = &s_pMonsters[i];
		if(!pMon->isActive) {
			continue;
		}
		// Sync the bob to the integrated position before culling/drawing.
		// The visual art is drawn ubDrawRaise px higher than the grounded
		// hitbox so ground walkers like the opossum stand with clearance.
		WORD wX = TO_PX(pMon->lPosX);
		WORD wY = TO_PX(pMon->lPosY) - pMon->ubDrawRaise;
		if(wX < 0) {
			continue;
		}
		pMon->sBob.sPos.uwX = (UWORD)wX;
		pMon->sBob.sPos.uwY = (UWORD)wY;

		if(wX > wViewR || wY > wViewB
			|| (wX + (WORD)pMon->sBob.uwWidth) < wViewL
			|| (wY + (WORD)pMon->sBob.uwHeight) < wViewT) {
			continue; // fully off-screen — skip (auto-undrawn by the manager)
		}
		bobPush(&pMon->sBob);
	}
	for(UBYTE i = 0; i < BOSS_SHOT_MAX; ++i) {
		tBossShot *pShot = &s_pBossShots[i];
		if(!pShot->isActive) {
			continue;
		}
		WORD wX = TO_PX(pShot->lPosX);
		WORD wY = TO_PX(pShot->lPosY);
		if(wX > wViewR || wY > wViewB
			|| (wX + (WORD)s_uwShotW) < wViewL
			|| (wY + (WORD)s_uwShotH) < wViewT) {
			continue;
		}
		bobPush(&pShot->sBob);
	}
	for(UBYTE i = 0; i < PACK_SHOT_MAX; ++i) {
		tPackShot *pShot = &s_pPackShots[i];
		if(!pShot->isActive) {
			continue;
		}
		WORD wX = TO_PX(pShot->lPosX);
		WORD wY = TO_PX(pShot->lPosY);
		if(wX > wViewR || wY > wViewB
			|| (wX + PIRANHA_BALL_FRAME_W) < wViewL
			|| (wY + PIRANHA_BALL_FRAME_H) < wViewT) {
			continue;
		}
		bobPush(&pShot->sBob);
	}
	for(UBYTE i = 0; i < GOO_MAX; ++i) {
		tGoo *pGoo = &s_pGoo[i];
		if(!pGoo->isActive) {
			continue;
		}
		WORD wX = TO_PX(pGoo->lPosX);
		WORD wY = TO_PX(pGoo->lPosY);
		if(wX > wViewR || wY > wViewB
			|| (wX + GOO_FRAME_W) < wViewL
			|| (wY + GOO_FRAME_H) < wViewT) {
			continue;
		}
		bobPush(&pGoo->sBob);
	}
}

void monstersDestroy(void) {
	bitmapDestroy(s_pWalker);
	bitmapDestroy(s_pWalkerMask);
	bitmapDestroy(s_pWalkerFlip);
	bitmapDestroy(s_pWalkerFlipMask);
	bitmapDestroy(s_pFlyer);
	bitmapDestroy(s_pFlyerMask);
	bitmapDestroy(s_pFlyerFlip);
	bitmapDestroy(s_pFlyerFlipMask);
	bitmapDestroy(s_pHopperIdle);
	bitmapDestroy(s_pHopperIdleMask);
	bitmapDestroy(s_pHopperIdleFlip);
	bitmapDestroy(s_pHopperIdleFlipMask);
	bitmapDestroy(s_pHopperJump);
	bitmapDestroy(s_pHopperJumpMask);
	bitmapDestroy(s_pHopperJumpFlip);
	bitmapDestroy(s_pHopperJumpFlipMask);
	bitmapDestroy(s_pBoss);
	bitmapDestroy(s_pBossMask);
	bitmapDestroy(s_pBossFlip);
	bitmapDestroy(s_pBossFlipMask);
	bitmapDestroy(s_pBossShot);
	bitmapDestroy(s_pBossShotMask);
	bitmapDestroy(s_pBossShotFlip);
	bitmapDestroy(s_pBossShotFlipMask);
	s_pWalker = 0; s_pWalkerMask = 0; s_pWalkerFlip = 0; s_pWalkerFlipMask = 0;
	s_pFlyer = 0; s_pFlyerMask = 0; s_pFlyerFlip = 0; s_pFlyerFlipMask = 0;
	s_pHopperIdle = 0; s_pHopperIdleMask = 0; s_pHopperIdleFlip = 0; s_pHopperIdleFlipMask = 0;
	s_pHopperJump = 0; s_pHopperJumpMask = 0; s_pHopperJumpFlip = 0; s_pHopperJumpFlipMask = 0;
	s_pBoss = 0; s_pBossMask = 0; s_pBossFlip = 0; s_pBossFlipMask = 0;
	s_pBossShot = 0; s_pBossShotMask = 0; s_pBossShotFlip = 0; s_pBossShotFlipMask = 0;
	bitmapDestroy(s_pWoods);
	bitmapDestroy(s_pWoodsMask);
	bitmapDestroy(s_pWoodsFlip);
	bitmapDestroy(s_pWoodsFlipMask);
	s_pWoods = 0; s_pWoodsMask = 0; s_pWoodsFlip = 0; s_pWoodsFlipMask = 0;
	bitmapDestroy(s_pGrasshopper);
	bitmapDestroy(s_pGrasshopperMask);
	bitmapDestroy(s_pGrasshopperFlip);
	bitmapDestroy(s_pGrasshopperFlipMask);
	s_pGrasshopper = 0; s_pGrasshopperMask = 0;
	s_pGrasshopperFlip = 0; s_pGrasshopperFlipMask = 0;
	bitmapDestroy(s_pPiranhaBall);
	bitmapDestroy(s_pPiranhaBallMask);
	bitmapDestroy(s_pPiranhaBallFlip);
	bitmapDestroy(s_pPiranhaBallFlipMask);
	s_pPiranhaBall = 0; s_pPiranhaBallMask = 0;
	s_pPiranhaBallFlip = 0; s_pPiranhaBallFlipMask = 0;
	bitmapDestroy(s_pGooBm);
	bitmapDestroy(s_pGooMask);
	s_pGooBm = 0; s_pGooMask = 0;
	destroyPackSheet(&s_sBeetle);
	destroyPackSheet(&s_sSlug);
	destroyPackSheet(&s_sSlimer);
	destroyPackSheet(&s_sDog);
	destroyPackSheet(&s_sAnt);
	destroyPackSheet(&s_sDino);
	destroyPackSheet(&s_sGator);
	destroyPackSheet(&s_sVulture);
	destroyPackSheet(&s_sBee);
	destroyPackSheet(&s_sPiranha);
	destroyPackSheet(&s_sPiranhaAtk);
}

UWORD monstersGetKilled(void) {
	return s_uwKilled;
}

UBYTE monstersHitWorldRect(WORD wL, WORD wT, WORD wR, WORD wB) {
	for(UWORD i = 0; i < s_uwEnemyCount; ++i) {
		tMonster *pMon = &s_pMonsters[i];
		if(!pMon->isActive) {
			continue;
		}
		WORD wEL = TO_PX(pMon->lPosX) + pMon->ubHbX;
		WORD wET = TO_PX(pMon->lPosY) + pMon->ubHbY;
		WORD wER = wEL + pMon->ubHbW - 1;
		WORD wEB = wET + pMon->ubHbH - 1;
		if(wL > wER || wR < wEL || wT > wEB || wB < wET) {
			continue;
		}
		monsterDamage(pMon);
		return 1;
	}
	return 0;
}

UBYTE monstersBossAlive(void) {
	return s_ubBossAlive;
}

UBYTE monstersGetBossHp(void) {
	return s_ubBossHp;
}

UBYTE monstersGetBossHpMax(void) {
	return s_ubBossHpMax;
}
