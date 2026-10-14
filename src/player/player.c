/* Player physics, tile collision, climb, and sprite draw. */
#include "player/player.h"
#include "ui/hud.h"
#include "game/game_defs.h"

#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include "debug/serial_log.h"
#include <ace/managers/memory.h>
#include <ace/utils/bitmap.h>

#include "assets/assets.h"
#include "world/collision.h"
#include "props/platforms.h"
#include "props/trampolines.h"
#include "props/vines.h"
#include "props/doors.h"
#include "audio/sfx.h"
#include <sprites/player.h>

//----------------------------------------------------------------- TUNING (50Hz)

#define PLAYER_WALK_SPEED 768    // ~3.0 px/frame
#define PLAYER_ICE_ACCEL 64      // ice: approach walk speed
#define PLAYER_ICE_FRICTION 32   // ice: slide to a stop
#define PLAYER_CONVEY_SPEED 256  // 1 px/frame belt carry
#define PLAYER_SAND_SINK 80      // slow fall in quicksand
#define PLAYER_SAND_JUMP 2048    // strong enough to hop out of a 2-tile pit
#define PLAYER_GRAVITY 128       // ~0.5 px/frame^2
#define PLAYER_JUMP_SPEED 2304   // ~9.0 px/frame (upwards)
#define PLAYER_CLIMB_SPEED 512   // ~2.0 px/frame on ladders
#define PLAYER_STOMP_BOUNCE 1728 // ~6.75 px/frame pop after squashing an enemy
#define PLAYER_TRAMP_BOUNCE 2688 // ~10.5 px/frame pad pop (above a jump)
#define PLAYER_TRAMP_HELD 3200   // ~12.5 px/frame if jump is held on contact
#define PLAYER_MAX_FALL 3200     // ~12.5 px/frame
#define PLAYER_SWING_INHERIT 3   // jump-off scales this-frame vine delta
#define PLAYER_SWING_HOP 768     // minimum upward hop so an apex jump is not a drop
#define PLAYER_SWING_LOCKOUT 12  // frames after release before a re-grab
#define PLAYER_COYOTE_FRAMES 6   // grace frames to jump after leaving ground

#define PLAYER_INVULN_FRAMES 75  // ~1.5s of i-frames after being hurt
#define PLAYER_HURT_LOCK 12      // frames control is locked out during knockback
#define PLAYER_KNOCKBACK_VX 640  // ~2.5 px/frame away from the attacker
#define PLAYER_KNOCKBACK_VY 1200 // ~4.7 px/frame upward pop

//------------------------------------------------------------------ HITBOX (px)
// Relative to the sprite's top-left. The 32px-wide frame has the fox art
// left-aligned; the hitbox hugs the body with the feet at the frame bottom.

#define HB_X 9
#define HB_W 15
#define HB_Y 7
#define HB_H 25

//----------------------------------------------------------------- ANIMATIONS
// Indices into s_player_Anim[] (see data/sprites/player.h).

#define ANIM_IDLE 0
#define ANIM_RUN 1
#define ANIM_CLIMB 2
#define ANIM_JUMP 5
#define ANIM_DUCK 3
#define ANIM_SURPRISE 8
#define ANIM_COMPLETE 11

// The generated s_player_Anim table groups every non-empty frame in a source
// row, so crouch becomes {14, 3}. Frame 14 is an unused pose; duck uses
// strip frames 15-16 only.
#define DUCK_FIRST_FRAME 15
#define DUCK_FRAME_COUNT 2

#define ANIM_IDLE_DELAY 8
#define ANIM_RUN_DELAY 4
#define ANIM_CLIMB_DELAY 6
#define ANIM_DUCK_DELAY 8
#define ANIM_SURPRISE_DELAY 5
#define DEATH_HOLD_FRAMES 28

//---------------------------------------------------------------------- STATE

static tTileBufferManager *s_pTileBuffer;

// Every strip frame prebuilt in sprite form, right-facing set then left-facing.
_Static_assert(PLAYER_FRAME_W == HUD_FOX_W, "fox sprite is 32px wide");
_Static_assert(PLAYER_FRAME_H == HUD_FOX_H, "fox sprite is 32px tall");
#define FOX_FRAME_COUNT (PLAYER_FRAME_COUNT * 2)
#define FOX_FRAMES_BYTES ((ULONG)FOX_FRAME_COUNT * HUD_FOX_SPR_FRAME_BYTES)
static UBYTE *s_pFoxFrames;

static LONG s_lPosX; // world X of sprite top-left, fixed point
static LONG s_lPosY; // world Y of sprite top-left, fixed point
static LONG s_lVelX;
static LONG s_lVelY;
static UBYTE s_isOnGround;
static UBYTE s_ubOnIce;
static UBYTE s_isInSand;
static UBYTE s_isDucking;
static UBYTE s_isClimbing;
static UBYTE s_isSwinging;
static UBYTE s_ubSwingLockout;
static UBYTE s_ubCoyote;
static UBYTE s_ubFacing;
static UBYTE s_ubJumpPrev;
static UBYTE s_ubLeftPrev;
static UBYTE s_ubRightPrev;

static UBYTE s_ubHealth;
static UBYTE s_ubInvuln;    // remaining invulnerability frames (0 = vulnerable)
static UBYTE s_ubHurtLock;  // remaining frames of input lockout during knockback
static UWORD s_uwSpawnTileX; // remembered spawn for respawn-on-death
static UWORD s_uwSpawnTileY;
static UBYTE s_ubRespawned; // set on death-respawn so the map can be redrawn
static UBYTE s_ubDied;      // set when the death surprise has finished
static UBYTE s_ubDying;     // playing the surprise anim after health hits zero
static UBYTE s_ubDeathHold; // hold last surprise frame before signalling died
static UBYTE s_ubLocked;    // no input (end-of-level card)
static UBYTE s_ubFrozen;    // no input (sign card; keep current pose)

static UBYTE s_ubAnim;
static UBYTE s_ubAnimFrame;
static UBYTE s_ubAnimTimer;
static UBYTE s_ubDrawFrame;
static UBYTE s_ubLastSprFrame;
static UBYTE s_ubLastSprFacing;

//------------------------------------------------------------- COLLISION HELPERS

// Set when stickToGround() snapped this frame. moveVertical() then skips the
// gravity + floor-sweep that would immediately undo 0.5px of fall into the
// same surface — the common grounded-walk path.
static UBYTE s_ubGlued;

// Is the player resting on ground right now? Probes the single pixel row just
// below the feet against solid/slope geometry (and one-ways on their top edge).
static UBYTE probeGround(void) {
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wBelow = TO_PX(s_lPosY) + HB_Y + HB_H; // first pixel below the feet
	if(s_lVelY >= 0 && platformsProbeGround(wBelow, wLeft, wRight)) {
		return 1;
	}
	if(s_lVelY >= 0 && doorsProbeGround(wBelow, wLeft, wRight)) {
		platformsClearRide();
		return 1;
	}
	if((wBelow & (TILE_SIZE - 1)) == 0) {
		// Flat tile top right under a foot — grounded without any span scan.
		// One-ways only support feet crossing their top edge, never on the way
		// up. HB_W spans at most two tiles, so these two columns are the whole
		// hitbox: no separate one-way scan is needed afterwards.
		UBYTE ubL = colAt(wLeft, wBelow);
		UBYTE ubR = colAt(wRight, wBelow);
		if(ubL == COL_SOLID || ubR == COL_SOLID) {
			platformsClearRide();
			return 1;
		}
		if(s_lVelY >= 0 && (colIsWalkTop(ubL) || colIsWalkTop(ubR))) {
			platformsClearRide();
			return 1;
		}
	}
	// Slopes, and surfaces on the bottom edge of the tile above (the foot of a
	// ramp), need the full probe.
	if(colHasFloor(wLeft, wBelow) || colHasFloor(wRight, wBelow)) {
		platformsClearRide();
		return 1;
	}
	platformsClearRide();
	return 0;
}

static void snapToSurface(WORD wSurf) {
	WORD wNewTopPx = (wSurf - 1) - (HB_Y + HB_H - 1);
	s_lPosY = PX(wNewTopPx);
	s_lVelY = 0;
	s_isOnGround = 1;
}

static LONG approachVel(LONG lCur, LONG lTarget, LONG lStep) {
	if(lCur < lTarget) {
		lCur += lStep;
		if(lCur > lTarget) {
			lCur = lTarget;
		}
	}
	else if(lCur > lTarget) {
		lCur -= lStep;
		if(lCur < lTarget) {
			lCur = lTarget;
		}
	}
	return lCur;
}

static UBYTE feetCol(void) {
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wBelow = TO_PX(s_lPosY) + HB_Y + HB_H;
	UBYTE ubL = colAt(wLeft, wBelow);
	UBYTE ubR = colAt(wRight, wBelow);
	if(colIsWalkTop(ubL) && ubL != COL_ONEWAY) {
		return ubL;
	}
	if(colIsWalkTop(ubR) && ubR != COL_ONEWAY) {
		return ubR;
	}
	return ubL ? ubL : ubR;
}

static UBYTE bodyInSand(void) {
	WORD wX = TO_PX(s_lPosX) + HB_X + HB_W / 2;
	WORD wTop = TO_PX(s_lPosY) + HB_Y;
	WORD wMid = wTop + HB_H / 2;
	WORD wFeet = wTop + HB_H - 1;
	return (
		colAt(wX, wTop) == COL_QUICKSAND ||
		colAt(wX, wMid) == COL_QUICKSAND ||
		colAt(wX, wFeet) == COL_QUICKSAND
	) ? 1 : 0;
}

static void applyDeltaX(LONG lDx) {
	if(!lDx) {
		return;
	}
	s_lPosX += lDx;
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wTop = TO_PX(s_lPosY) + HB_Y;
	WORD wBottom = wTop + HB_H - 1;
	if(colBlockedVSpan(wRight, wTop, wBottom)) {
		WORD wSnapRight = (wRight & ~(TILE_SIZE - 1)) - 1;
		s_lPosX = PX(wSnapRight - (HB_X + HB_W - 1));
	}
	else if(colBlockedVSpan(wLeft, wTop, wBottom)) {
		WORD wSnapLeft = (wLeft & ~(TILE_SIZE - 1)) + TILE_SIZE;
		s_lPosX = PX(wSnapLeft - HB_X);
	}
	else {
		WORD wEdge = doorsSnapX(wRight, wTop, wBottom, 1);
		if(wEdge >= 0) {
			s_lPosX = PX(wEdge - (HB_X + HB_W - 1));
		}
		else {
			wEdge = doorsSnapX(wLeft, wTop, wBottom, 0);
			if(wEdge >= 0) {
				s_lPosX = PX(wEdge - HB_X);
			}
		}
	}
}

static void applyPlatformCarry(void) {
	s_lPosX += platformsRideDeltaX();
	s_lPosY += platformsRideDeltaY();

	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wTop = TO_PX(s_lPosY) + HB_Y;
	WORD wBottom = wTop + HB_H - 1;
	if(colBlockedVSpan(wRight, wTop, wBottom)) {
		WORD wSnapRight = (wRight & ~(TILE_SIZE - 1)) - 1;
		s_lPosX = PX(wSnapRight - (HB_X + HB_W - 1));
		wLeft = TO_PX(s_lPosX) + HB_X;
		wRight = wLeft + HB_W - 1;
	}
	else if(colBlockedVSpan(wLeft, wTop, wBottom)) {
		WORD wSnapLeft = (wLeft & ~(TILE_SIZE - 1)) + TILE_SIZE;
		s_lPosX = PX(wSnapLeft - HB_X);
		wLeft = TO_PX(s_lPosX) + HB_X;
		wRight = wLeft + HB_W - 1;
	}
	{
		WORD wEdge = doorsSnapX(wRight, wTop, wBottom, 1);
		if(wEdge >= 0) {
			s_lPosX = PX(wEdge - (HB_X + HB_W - 1));
			wLeft = TO_PX(s_lPosX) + HB_X;
			wRight = wLeft + HB_W - 1;
		}
		else {
			wEdge = doorsSnapX(wLeft, wTop, wBottom, 0);
			if(wEdge >= 0) {
				s_lPosX = PX(wEdge - HB_X);
				wLeft = TO_PX(s_lPosX) + HB_X;
				wRight = wLeft + HB_W - 1;
			}
		}
	}

	// Keep feet glued to a vertical mover. Exact pixel probes miss after an
	// 8.8 step, then gravity cannot recatch a platform that already sits
	// under the feet.
	WORD wSurf;
	if(platformsStickRide(wLeft, wRight, &wSurf)) {
		snapToSurface(wSurf);
	}
}

// After walking while grounded, stay glued to ramps:
//  1) If feet are embedded in a solid/slope, push up to that surface.
//  2) Otherwise snap down at most one tile onto a surface (walk down slopes).
// Never snap upward onto a surface the feet are not inside — that pulled the
// player off solid ground under floating slope tiles.
static UBYTE stickToGround(void) {
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wFeet = TO_PX(s_lPosY) + HB_Y + HB_H - 1;
	WORD wSurf;
	if(!colStickFeet(wLeft, wRight, wFeet, &wSurf)) {
		return 0;
	}
	WORD wNewTopPx = (wSurf - 1) - (HB_Y + HB_H - 1);
	if(wNewTopPx != TO_PX(s_lPosY)) {
		s_lPosY = PX(wNewTopPx);
	}
	s_lVelY = 0;
	return 1;
}

//---------------------------------------------------------------------- INPUT

// JOY1 is Amiga port 2 (the stick). The mouse is port 1 / ACE JOY2.
static UBYTE inputLeft(void) {
	return keyCheck(KEY_LEFT) || keyCheck(KEY_A) || joyCheck(JOY1_LEFT);
}

static UBYTE inputRight(void) {
	return keyCheck(KEY_RIGHT) || keyCheck(KEY_D) || joyCheck(JOY1_RIGHT);
}

static UBYTE inputUp(void) {
	return keyCheck(KEY_UP) || keyCheck(KEY_W) || joyCheck(JOY1_UP);
}

static UBYTE inputJump(void) {
	// Climb hop is Space only (see moveClimb). Off a ladder, Up / stick up jump.
	return keyCheck(KEY_SPACE) || keyCheck(KEY_UP) || keyCheck(KEY_W) ||
		joyCheck(JOY1_UP);
}

static UBYTE inputDown(void) {
	return keyCheck(KEY_DOWN) || keyCheck(KEY_S) || joyCheck(JOY1_DOWN);
}

//-------------------------------------------------------------------- LADDERS

static void playerKill(void);

static WORD hitboxCenterX(void) {
	return TO_PX(s_lPosX) + HB_X + HB_W / 2;
}

static UBYTE bodyOverlapsLadder(void) {
	WORD wTop = TO_PX(s_lPosY) + HB_Y;
	WORD wBottom = wTop + HB_H - 1;
	return colLadderInSpan(hitboxCenterX(), wTop, wBottom);
}

static UBYTE ladderBelowFeet(void) {
	// First pixel under the feet, plus the tile below that — standing on a
	// one-way puts the ladder in the cell under the platform, not in the
	// platform tile itself.
	WORD wX = hitboxCenterX();
	WORD wBelow = TO_PX(s_lPosY) + HB_Y + HB_H;
	return colLadderInSpan(wX, wBelow, wBelow + TILE_SIZE);
}

static void snapToLadderX(void) {
	WORD wTileLeft = hitboxCenterX() & ~(TILE_SIZE - 1);
	s_lPosX = PX(wTileLeft + TILE_SIZE / 2 - (HB_X + HB_W / 2));
}

static void leaveClimb(void) {
	s_isClimbing = 0;
}

static void leaveSwing(void) {
	if(s_isSwinging) {
		s_ubSwingLockout = PLAYER_SWING_LOCKOUT;
	}
	s_isSwinging = 0;
	vinesClearGrab();
}

static void enterClimb(void) {
	s_isClimbing = 1;
	s_isDucking = 0;
	s_isOnGround = 0;
	s_ubCoyote = 0;
	s_ubGlued = 0;
	s_lVelX = 0;
	s_lVelY = 0;
	platformsClearRide();
	snapToLadderX();
	// A run-in that is still holding Left/Right must not hop off this frame.
	s_ubLeftPrev = inputLeft();
	s_ubRightPrev = inputRight();
}

static void checkPitDeath(void) {
	if(s_ubDying || s_ubLocked) {
		return;
	}
	WORD wFeet = TO_PX(s_lPosY) + HB_Y + HB_H;
	WORD wWorldBottom = (WORD)(s_pTileBuffer->uTileBounds.uwY << 4);
	WORD wViewBottom = (WORD)s_pTileBuffer->pCamera->uPos.uwY + SCREEN_H;
	if(wFeet >= wWorldBottom || wFeet >= wViewBottom) {
		playerKill();
	}
}

static void landFromClimb(void) {
	leaveClimb();
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;
	WORD wFeet = TO_PX(s_lPosY) + HB_Y + HB_H - 1;
	WORD wSurf;
	if(colStickFeet(wLeft, wRight, wFeet, &wSurf)) {
		snapToSurface(wSurf);
	}
	else if(probeGround()) {
		s_isOnGround = 1;
		s_lVelY = 0;
	}
	else {
		s_isOnGround = 0;
		if(s_lVelY < 0) {
			s_lVelY = 0;
		}
	}
}

static void moveClimb(void);
static void moveSwing(void);
static void moveHorizontal(void);
static void moveVertical(void);

static void enterSwing(void) {
	s_isSwinging = 1;
	s_isDucking = 0;
	s_isOnGround = 0;
	s_ubCoyote = 0;
	s_ubGlued = 0;
	s_lVelX = 0;
	s_lVelY = 0;
	leaveClimb();
	platformsClearRide();
	// Held Jump / Left / Right must not hop off the same frame we grab.
	s_ubJumpPrev = inputJump();
	s_ubLeftPrev = inputLeft();
	s_ubRightPrev = inputRight();
}

static void moveSwing(void) {
	LONG lGrabX = s_lPosX;
	LONG lGrabY = s_lPosY;
	LONG lDx = 0;
	LONG lDy = 0;
	vinesGrabPos(&lGrabX, &lGrabY);
	vinesGrabDelta(&lDx, &lDy);
	/* Hands on the pendulum knot; body hangs below (climb pose). */
	s_lPosX = lGrabX - PX(HB_X + HB_W / 2);
	s_lPosY = lGrabY - PX(HB_Y);
	s_isOnGround = 0;
	s_lVelX = 0;
	s_lVelY = 0;
	if(lDx < 0) {
		s_ubFacing = PLAYER_FACE_LEFT;
	}
	else if(lDx > 0) {
		s_ubFacing = PLAYER_FACE_RIGHT;
	}

	UBYTE isJump = inputJump();
	UBYTE isLeft = inputLeft() && !inputRight();
	UBYTE isRight = inputRight() && !inputLeft();
	UBYTE isHop = (
		(isJump && !s_ubJumpPrev) ||
		(isLeft && !s_ubLeftPrev) ||
		(isRight && !s_ubRightPrev)
	);
	if(isHop) {
		leaveSwing();
		s_lVelX = lDx * PLAYER_SWING_INHERIT;
		s_lVelY = lDy * PLAYER_SWING_INHERIT;
		if(isLeft) {
			s_lVelX -= PLAYER_WALK_SPEED;
			s_ubFacing = PLAYER_FACE_LEFT;
		}
		else if(isRight) {
			s_lVelX += PLAYER_WALK_SPEED;
			s_ubFacing = PLAYER_FACE_RIGHT;
		}
		if(s_lVelY > -PLAYER_SWING_HOP) {
			s_lVelY -= PLAYER_SWING_HOP;
		}
		s_ubCoyote = 0;
		s_ubGlued = 0;
		sfxPlayJump();
		s_ubJumpPrev = 1;
		s_ubLeftPrev = isLeft;
		s_ubRightPrev = isRight;
		moveHorizontal();
		moveVertical();
		return;
	}
	s_ubJumpPrev = isJump;
	s_ubLeftPrev = isLeft;
	s_ubRightPrev = isRight;
}

//------------------------------------------------------------------- MOVEMENT

static void moveHorizontal(void) {
	UBYTE isWasGrounded = s_isOnGround;
	UBYTE isMovedX = (s_lVelX != 0);

	if(isMovedX) {
		LONG lNewX = s_lPosX + s_lVelX;
		WORD wNewLeftPx = TO_PX(lNewX);

		WORD wTop = TO_PX(s_lPosY) + HB_Y;
		WORD wBottom = wTop + HB_H - 1;

		if(s_lVelX > 0) {
			WORD wRight = wNewLeftPx + HB_X + HB_W - 1;
			if(colBlockedVSpan(wRight, wTop, wBottom)) {
				// Snap so the right edge is just left of the blocking tile.
				WORD wSnapRight = (wRight & ~(TILE_SIZE - 1)) - 1;
				wNewLeftPx = wSnapRight - (HB_X + HB_W - 1);
				lNewX = PX(wNewLeftPx);
				s_lVelX = 0;
			}
			{
				WORD wEdge = doorsSnapX(wRight, wTop, wBottom, 1);
				if(wEdge >= 0) {
					wNewLeftPx = wEdge - (HB_X + HB_W - 1);
					lNewX = PX(wNewLeftPx);
					s_lVelX = 0;
				}
			}
		}
		else {
			WORD wLeft = wNewLeftPx + HB_X;
			if(colBlockedVSpan(wLeft, wTop, wBottom)) {
				// Snap so the left edge is just right of the blocking tile.
				WORD wSnapLeft = (wLeft & ~(TILE_SIZE - 1)) + TILE_SIZE;
				wNewLeftPx = wSnapLeft - HB_X;
				lNewX = PX(wNewLeftPx);
				s_lVelX = 0;
			}
			{
				WORD wEdge = doorsSnapX(wLeft, wTop, wBottom, 0);
				if(wEdge >= 0) {
					wNewLeftPx = wEdge - HB_X;
					lNewX = PX(wNewLeftPx);
					s_lVelX = 0;
				}
			}
		}
		s_lPosX = lNewX;
	}

	// Ride slopes: after a grounded step, snap onto the nearby surface so
	// walking up a ramp (feet embed, push up) or down (snap down) does not
	// launch or fall through. Standing still cannot change the floor.
	s_ubGlued = 0;
	if(isWasGrounded) {
		WORD wLeft = TO_PX(s_lPosX) + HB_X;
		WORD wRight = wLeft + HB_W - 1;
		WORD wSurf;
		if(platformsStickRide(wLeft, wRight, &wSurf)) {
			snapToSurface(wSurf);
			s_ubGlued = 1;
		}
		else {
			s_ubGlued = isMovedX ? stickToGround() : 1;
		}
	}
}

static void playerKill(void) {
	sfxPlayHurt();
	s_ubHealth = 0;
	s_ubDying = 1;
	leaveClimb();
	leaveSwing();
	platformsClearRide();
	s_ubDeathHold = 0;
	s_ubInvuln = 0;
	s_ubHurtLock = 0;
	s_lVelX = 0;
	s_ubAnim = 0xFF; // force setAnim to restart surprise
	s_ubAnimFrame = 0;
	s_ubAnimTimer = 0;
}

static void moveVertical(void) {
	// Stick already snapped this frame. Skip gravity only if the pixel under
	// the feet is still a floor — otherwise we left the surface we were glued
	// to (walked off a plateau onto a ramp, or lost the uphill) and must fall.
	if(s_ubGlued) {
		s_ubGlued = 0;
		if(probeGround()) {
			s_isOnGround = 1;
			return;
		}
	}

	s_lVelY += PLAYER_GRAVITY;
	if(s_isInSand && s_lVelY > PLAYER_SAND_SINK) {
		s_lVelY = PLAYER_SAND_SINK;
	}
	else if(s_lVelY > PLAYER_MAX_FALL) {
		s_lVelY = PLAYER_MAX_FALL;
	}

	WORD wOldBottom = TO_PX(s_lPosY) + HB_Y + HB_H - 1;
	LONG lNewY = s_lPosY + s_lVelY;
	WORD wNewTopPx = TO_PX(lNewY);

	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;

	if(s_lVelY > 0) {
		WORD wBottom = wNewTopPx + HB_Y + HB_H - 1;
		WORD wHit = colSweepFloor(wOldBottom, wBottom, wLeft, wRight);
		UBYTE isLand = (wHit >= 0);
		if(!isLand) {
			// One-way platforms only catch feet crossing their top edge.
			WORD wPlatformTop = wBottom & ~(TILE_SIZE - 1);
			if(wOldBottom < wPlatformTop && colOneWayHSpan(wBottom, wLeft, wRight)) {
				wHit = wPlatformTop;
				isLand = 1;
			}
		}
		{
			WORD wPlatHit = platformsTryLand(wOldBottom, wBottom, wLeft, wRight);
			if(wPlatHit >= 0 && (!isLand || wPlatHit <= wHit)) {
				wHit = wPlatHit;
				isLand = 1;
			}
			else if(isLand) {
				platformsClearRide();
			}
		}
		{
			WORD wDoorHit = doorsTryLand(wOldBottom, wBottom, wLeft, wRight);
			if(wDoorHit >= 0 && (!isLand || wDoorHit <= wHit)) {
				wHit = wDoorHit;
				isLand = 1;
			}
		}
		if(isLand) {
			wNewTopPx = (wHit - 1) - (HB_Y + HB_H - 1);
			lNewY = PX(wNewTopPx);
			s_lVelY = 0;
		}
		{
			WORD wTramp = trampolinesTryBounce(wOldBottom, wBottom, wLeft, wRight);
			if(wTramp >= 0) {
				wNewTopPx = (wTramp - 1) - (HB_Y + HB_H - 1);
				lNewY = PX(wNewTopPx);
				s_lPosY = lNewY;
				playerTrampoline();
				s_isOnGround = 0;
				checkPitDeath();
				return;
			}
		}
	}
	else if(s_lVelY < 0) {
		WORD wHead = wNewTopPx + HB_Y;
		if(colBlockedCeiling(wHead, wLeft, wRight)) {
			WORD wSnapHead = (wHead & ~(TILE_SIZE - 1)) + TILE_SIZE;
			wNewTopPx = wSnapHead - HB_Y;
			lNewY = PX(wNewTopPx);
			s_lVelY = 0;
		}
		{
			WORD wDoorHead = doorsSnapCeiling(wHead, wLeft, wRight);
			if(wDoorHead >= 0) {
				wNewTopPx = wDoorHead - HB_Y;
				lNewY = PX(wNewTopPx);
				s_lVelY = 0;
			}
		}
	}
	s_lPosY = lNewY;

	// Determine grounded state from the tiles directly under the feet, so it
	// stays stable while resting (see probeGround). While rising (velY < 0) we
	// are definitively airborne, so skip the span scan probeGround would do.
	s_isOnGround = (s_lVelY < 0) ? 0 : probeGround();

	// Pit: leaving the map or the bottom of the view is always lethal.
	// (The world is shorter than the PAL display, and the row below it used
	// to be a solid floor — so you could stand in a hole and never die.)
	checkPitDeath();
}

static void moveClimb(void) {
	UBYTE isUp = inputUp();
	UBYTE isDown = inputDown();
	UBYTE isJump = keyCheck(KEY_SPACE);
	UBYTE isLeft = inputLeft() && !inputRight();
	UBYTE isRight = inputRight() && !inputLeft();
	UBYTE isHop = (
		(isJump && !s_ubJumpPrev) ||
		(isLeft && !s_ubLeftPrev) ||
		(isRight && !s_ubRightPrev)
	);

	if(isHop) {
		leaveClimb();
		if(isLeft) {
			s_lVelX = -PLAYER_WALK_SPEED;
			s_ubFacing = PLAYER_FACE_LEFT;
		}
		else if(isRight) {
			s_lVelX = PLAYER_WALK_SPEED;
			s_ubFacing = PLAYER_FACE_RIGHT;
		}
		s_lVelY = -PLAYER_JUMP_SPEED;
		s_isOnGround = 0;
		s_ubCoyote = 0;
		platformsClearRide();
		sfxPlayJump();
		s_ubJumpPrev = 1;
		s_ubLeftPrev = isLeft;
		s_ubRightPrev = isRight;
		s_ubGlued = 0;
		moveHorizontal();
		moveVertical();
		return;
	}

	s_lVelX = 0;
	if(isUp && !isDown) {
		s_lVelY = -PLAYER_CLIMB_SPEED;
	}
	else if(isDown && !isUp) {
		s_lVelY = PLAYER_CLIMB_SPEED;
	}
	else {
		s_lVelY = 0;
	}

	WORD wOldBottom = TO_PX(s_lPosY) + HB_Y + HB_H - 1;
	LONG lNewY = s_lPosY + s_lVelY;
	WORD wNewTopPx = TO_PX(lNewY);
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	WORD wRight = wLeft + HB_W - 1;

	if(s_lVelY < 0) {
		WORD wHead = wNewTopPx + HB_Y;
		if(colBlockedCeiling(wHead, wLeft, wRight)) {
			WORD wSnapHead = (wHead & ~(TILE_SIZE - 1)) + TILE_SIZE;
			wNewTopPx = wSnapHead - HB_Y;
			lNewY = PX(wNewTopPx);
			s_lVelY = 0;
		}
	}
	else if(s_lVelY > 0) {
		WORD wBottom = wNewTopPx + HB_Y + HB_H - 1;
		WORD wHit = colSweepFloor(wOldBottom, wBottom, wLeft, wRight);
		if(wHit >= 0) {
			wNewTopPx = (wHit - 1) - (HB_Y + HB_H - 1);
			lNewY = PX(wNewTopPx);
			s_lPosY = lNewY;
			s_lVelY = 0;
			leaveClimb();
			s_isOnGround = 1;
			s_ubJumpPrev = isJump;
			s_ubLeftPrev = isLeft;
			s_ubRightPrev = isRight;
			checkPitDeath();
			return;
		}
		// Drop through one-ways while holding Down onto a ladder; otherwise
		// land on them the same way falling does.
		if(!(isDown && (bodyOverlapsLadder() || ladderBelowFeet()))) {
			WORD wPlatformTop = wBottom & ~(TILE_SIZE - 1);
			if(wOldBottom < wPlatformTop && colOneWayHSpan(wBottom, wLeft, wRight)) {
				wNewTopPx = (wPlatformTop - 1) - (HB_Y + HB_H - 1);
				lNewY = PX(wNewTopPx);
				s_lPosY = lNewY;
				s_lVelY = 0;
				leaveClimb();
				s_isOnGround = 1;
				s_ubJumpPrev = isJump;
				s_ubLeftPrev = isLeft;
				s_ubRightPrev = isRight;
				checkPitDeath();
				return;
			}
		}
	}

	s_lPosY = lNewY;

	if(bodyOverlapsLadder() || (s_lVelY > 0 && ladderBelowFeet())) {
		s_isOnGround = 0;
	}
	else {
		landFromClimb();
	}

	s_ubJumpPrev = isJump;
	s_ubLeftPrev = isLeft;
	s_ubRightPrev = isRight;
	checkPitDeath();
}

//---------------------------------------------------------------- ANIMATION

static void setAnim(UBYTE ubAnim) {
	if(s_ubAnim != ubAnim) {
		s_ubAnim = ubAnim;
		s_ubAnimFrame = 0;
		s_ubAnimTimer = 0;
	}
}

static void animate(void) {
	UBYTE ubGlobalFrame;

	if(s_isSwinging) {
		setAnim(ANIM_CLIMB);
		ubGlobalFrame = s_player_Anim[ANIM_CLIMB][0];
	}
	else if(s_isClimbing) {
		setAnim(ANIM_CLIMB);
		if(s_lVelY != 0) {
			if(++s_ubAnimTimer >= ANIM_CLIMB_DELAY) {
				s_ubAnimTimer = 0;
				if(++s_ubAnimFrame >= s_player_Anim[ANIM_CLIMB][1]) {
					s_ubAnimFrame = 0;
				}
			}
		}
		ubGlobalFrame = s_player_Anim[ANIM_CLIMB][0] + s_ubAnimFrame;
	}
	else if(!s_isOnGround) {
		setAnim(ANIM_JUMP);
		// Rising uses the take-off frame, falling uses the second frame.
		UBYTE ubSub = (s_lVelY < 0) ? 0 : 1;
		if(ubSub >= s_player_Anim[ANIM_JUMP][1]) {
			ubSub = s_player_Anim[ANIM_JUMP][1] - 1;
		}
		ubGlobalFrame = s_player_Anim[ANIM_JUMP][0] + ubSub;
	}
	else if(s_isDucking) {
		setAnim(ANIM_DUCK);
		if(++s_ubAnimTimer >= ANIM_DUCK_DELAY) {
			s_ubAnimTimer = 0;
			if(++s_ubAnimFrame >= DUCK_FRAME_COUNT) {
				s_ubAnimFrame = 0;
			}
		}
		ubGlobalFrame = DUCK_FIRST_FRAME + s_ubAnimFrame;
	}
	else {
		UBYTE ubDelay;
		if(s_lVelX != 0) {
			setAnim(ANIM_RUN);
			ubDelay = ANIM_RUN_DELAY;
		}
		else {
			setAnim(ANIM_IDLE);
			ubDelay = ANIM_IDLE_DELAY;
		}
		if(++s_ubAnimTimer >= ubDelay) {
			s_ubAnimTimer = 0;
			if(++s_ubAnimFrame >= s_player_Anim[s_ubAnim][1]) {
				s_ubAnimFrame = 0;
			}
		}
		ubGlobalFrame = s_player_Anim[s_ubAnim][0] + s_ubAnimFrame;
	}

	s_ubDrawFrame = ubGlobalFrame;
}

//------------------------------------------------------------------- PUBLIC

static const UBYTE *foxFrame(UBYTE ubFacing, UBYTE ubFrame) {
	if(ubFrame >= PLAYER_FRAME_COUNT) {
		ubFrame = 0;
	}
	UWORD uwIndex = (UWORD)(
		(ubFacing == PLAYER_FACE_RIGHT ? 0 : PLAYER_FRAME_COUNT) + ubFrame
	);
	return s_pFoxFrames + (ULONG)uwIndex * HUD_FOX_SPR_FRAME_BYTES;
}

void playerLoad(tTileBufferManager *pTileBuffer) {
	s_pTileBuffer = pTileBuffer;

	// The art ships as a 4bpp planar strip, but the chained fox needs
	// attached-pair sprite data. Build every frame up front: doing it on the
	// frame an animation advanced cost a third of a PAL frame, so running
	// hitched every 4 ticks while a held jump pose never paid it at all.
	// Once converted the strips are dead weight, so the chip RAM goes back.
	tBitMap *pFrames = sunnyLoadBitmap("data/sprites/player.bm", 0);
	tBitMap *pFramesFlip = sunnyLoadBitmap("data/sprites/player_flip.bm", 0);
	s_pFoxFrames = memAllocFast(FOX_FRAMES_BYTES);
	if(!s_pFoxFrames) {
		logWrite("ERROR: out of memory for %lu bytes of fox frames\n", FOX_FRAMES_BYTES);
	}
	else {
		for(UBYTE i = 0; i < PLAYER_FRAME_COUNT; ++i) {
			UWORD uwOffsY = (UWORD)i * PLAYER_FRAME_H;
			hudBuildFoxFrame(
				pFrames, uwOffsY,
				s_pFoxFrames + (ULONG)i * HUD_FOX_SPR_FRAME_BYTES
			);
			hudBuildFoxFrame(
				pFramesFlip, uwOffsY,
				s_pFoxFrames
					+ (ULONG)(PLAYER_FRAME_COUNT + i) * HUD_FOX_SPR_FRAME_BYTES
			);
		}
	}
	bitmapDestroy(pFrames);
	bitmapDestroy(pFramesFlip);
}

void playerReset(UWORD uwSpawnTileX, UWORD uwSpawnTileY) {
	// Center the hitbox horizontally on the spawn tile and rest the feet on
	// the bottom edge of the spawn tile row (gravity settles it next frame).
	WORD wHitboxLeft = (WORD)(uwSpawnTileX << 4) + (TILE_SIZE - HB_W) / 2;
	WORD wFeetY = (WORD)((uwSpawnTileY + 1) << 4);

	s_uwSpawnTileX = uwSpawnTileX;
	s_uwSpawnTileY = uwSpawnTileY;

	s_lPosX = PX(wHitboxLeft - HB_X);
	s_lPosY = PX(wFeetY - (HB_Y + HB_H));
	s_lVelX = 0;
	s_lVelY = 0;
	s_isOnGround = 0;
	s_ubOnIce = 0;
	s_isInSand = 0;
	s_isDucking = 0;
	s_isClimbing = 0;
	s_isSwinging = 0;
	s_ubSwingLockout = 0;
	s_ubCoyote = 0;
	s_ubFacing = PLAYER_FACE_RIGHT;
	s_ubJumpPrev = 0;
	s_ubLeftPrev = 0;
	s_ubRightPrev = 0;
	s_ubHealth = PLAYER_START_HEALTH;
	s_ubInvuln = 0;
	s_ubHurtLock = 0;
	s_ubRespawned = 0;
	s_ubDied = 0;
	s_ubDying = 0;
	s_ubDeathHold = 0;
	s_ubLocked = 0;
	s_ubFrozen = 0;
	s_ubGlued = 0;
	platformsClearRide();
	vinesClearGrab();
	s_ubAnim = ANIM_IDLE;
	s_ubAnimFrame = 0;
	s_ubAnimTimer = 0;
	s_ubDrawFrame = 0;
	s_ubLastSprFrame = 0xFF;
	s_ubLastSprFacing = 0xFF;
}

static void animateDeath(void) {
	setAnim(ANIM_SURPRISE);
	UBYTE ubCount = (UBYTE)s_player_Anim[ANIM_SURPRISE][1];
	if(s_ubAnimFrame + 1 < ubCount) {
		if(++s_ubAnimTimer >= ANIM_SURPRISE_DELAY) {
			s_ubAnimTimer = 0;
			++s_ubAnimFrame;
		}
	}
	else if(s_ubDeathHold < DEATH_HOLD_FRAMES) {
		++s_ubDeathHold;
	}
	else {
		s_ubDied = 1;
	}
	s_ubDrawFrame = (UBYTE)(s_player_Anim[ANIM_SURPRISE][0] + s_ubAnimFrame);
}

void playerLock(void) {
	s_ubLocked = 1;
	s_ubFrozen = 0;
	s_lVelX = 0;
	leaveClimb();
	leaveSwing();
}

void playerFreeze(void) {
	s_ubFrozen = 1;
	s_lVelX = 0;
	leaveClimb();
	leaveSwing();
}

void playerUnlock(void) {
	s_ubFrozen = 0;
}

void playerProcess(void) {
	// Tick down invulnerability every frame regardless of control state.
	if(s_ubInvuln) {
		--s_ubInvuln;
	}

	if(s_ubDying) {
		s_lVelX = 0;
		leaveClimb();
		leaveSwing();
		animateDeath();
		s_ubJumpPrev = 0;
		return;
	}

	if(s_isClimbing) {
		// End-of-level and knockback drop off the rail into normal settle.
		if(s_ubLocked || s_ubFrozen || s_ubHurtLock) {
			leaveClimb();
		}
		else {
			moveClimb();
			if(!s_ubDying) {
				animate();
			}
			return;
		}
	}

	if(s_isSwinging) {
		if(s_ubLocked || s_ubFrozen || s_ubHurtLock) {
			leaveSwing();
		}
		else {
			moveSwing();
			if(!s_ubDying) {
				animate();
			}
			return;
		}
	}

	applyPlatformCarry();

	// End-of-level freeze: settle with gravity, hold the looking-at-camera pose.
	if(s_ubLocked) {
		s_lVelX = 0;
		s_isDucking = 0;
		s_ubFacing = PLAYER_FACE_RIGHT; // anim 11 faces the camera
		if(s_ubHurtLock) {
			--s_ubHurtLock;
		}
		s_ubGlued = 0;
		moveVertical();
		setAnim(ANIM_COMPLETE);
		s_ubDrawFrame = (UBYTE)s_player_Anim[ANIM_COMPLETE][0];
		s_ubJumpPrev = 0;
		return;
	}

	// Sign card: no input, keep the current pose, gravity still settles.
	if(s_ubFrozen) {
		s_lVelX = 0;
		s_isDucking = 0;
		if(s_ubHurtLock) {
			--s_ubHurtLock;
		}
		s_ubGlued = 0;
		moveVertical();
		s_ubJumpPrev = 0;
		return;
	}

	// While knocked back, control is locked: keep the knockback velocity and
	// let physics/collision run, but ignore movement and jump input.
	if(s_ubHurtLock) {
		--s_ubHurtLock;
		moveHorizontal();
		moveVertical();
		if(s_ubDying) {
			s_ubJumpPrev = 0;
			return;
		}
		animate();
		s_ubJumpPrev = 0; // require a fresh press once control returns
		return;
	}

	UBYTE isUp = inputUp();
	UBYTE isDown = inputDown();
	UBYTE isOverlap = bodyOverlapsLadder();
	UBYTE isLadderBelow = ladderBelowFeet();
	UBYTE isClimbIntent = (
		(isUp && isOverlap) ||
		(isDown && (isOverlap || (s_isOnGround && isLadderBelow)))
	);
	if(isClimbIntent) {
		enterClimb();
		moveClimb();
		if(!s_ubDying) {
			animate();
		}
		return;
	}

	if(s_ubSwingLockout) {
		--s_ubSwingLockout;
	}
	else {
		WORD wLeft = TO_PX(s_lPosX) + HB_X;
		WORD wTop = TO_PX(s_lPosY) + HB_Y;
		WORD wRight = wLeft + HB_W - 1;
		WORD wBottom = wTop + HB_H - 1;
		if(vinesTryGrab(wLeft, wTop, wRight, wBottom)) {
			enterSwing();
			moveSwing();
			if(!s_ubDying) {
				animate();
			}
			return;
		}
	}

	// Ducking: crouch when holding down on the ground. It suppresses walking
	// (you stop to duck), matching the usual platformer feel. Ice still slides.
	s_isDucking = (s_isOnGround && isDown && !s_ubOnIce);
	s_isInSand = bodyInSand();
	UBYTE ubFloor = s_isOnGround ? feetCol() : COL_EMPTY;
	if(s_isOnGround) {
		s_ubOnIce = (ubFloor == COL_ICE) ? 1 : 0;
	}

	UBYTE isIceSlide = (UBYTE)(s_ubOnIce || (!s_isOnGround && s_ubOnIce));
	if(isIceSlide) {
		LONG lTarget = 0;
		if(!s_isDucking) {
			if(inputLeft() && !inputRight()) {
				lTarget = -PLAYER_WALK_SPEED;
				s_ubFacing = PLAYER_FACE_LEFT;
			}
			else if(inputRight() && !inputLeft()) {
				lTarget = PLAYER_WALK_SPEED;
				s_ubFacing = PLAYER_FACE_RIGHT;
			}
		}
		if(lTarget) {
			s_lVelX = approachVel(s_lVelX, lTarget, PLAYER_ICE_ACCEL);
		}
		else if(s_isOnGround) {
			s_lVelX = approachVel(s_lVelX, 0, PLAYER_ICE_FRICTION);
		}
	}
	else {
		s_lVelX = 0;
		if(!s_isDucking) {
			if(inputLeft() && !inputRight()) {
				s_lVelX = -PLAYER_WALK_SPEED;
				s_ubFacing = PLAYER_FACE_LEFT;
			}
			else if(inputRight() && !inputLeft()) {
				s_lVelX = PLAYER_WALK_SPEED;
				s_ubFacing = PLAYER_FACE_RIGHT;
			}
		}
	}
	if(s_isInSand) {
		s_lVelX >>= 1;
	}

	// Coyote time: allow a jump for a few frames after walking off a ledge.
	// Quicksand is not a floor, but you can always hop out while in it.
	if(s_isOnGround || s_isInSand) {
		s_ubCoyote = PLAYER_COYOTE_FRAMES;
	}
	else if(s_ubCoyote) {
		--s_ubCoyote;
	}

	if(s_isOnGround) {
		if(ubFloor == COL_CONVEY_L) {
			applyDeltaX(-PLAYER_CONVEY_SPEED);
		}
		else if(ubFloor == COL_CONVEY_R) {
			applyDeltaX(PLAYER_CONVEY_SPEED);
		}
	}

	UBYTE isJump = inputJump();
	if(isJump && !s_ubJumpPrev && s_ubCoyote) {
		s_lVelY = s_isInSand ? -PLAYER_SAND_JUMP : -PLAYER_JUMP_SPEED;
		s_lVelX += platformsRideVelX();
		if(ubFloor == COL_CONVEY_L) {
			s_lVelX -= PLAYER_CONVEY_SPEED;
		}
		else if(ubFloor == COL_CONVEY_R) {
			s_lVelX += PLAYER_CONVEY_SPEED;
		}
		s_isOnGround = 0;
		s_ubCoyote = 0;
		platformsClearRide();
		sfxPlayJump();
	}
	// Variable jump height: releasing jump while rising cuts upward speed.
	if(!isJump && s_lVelY < 0) {
		s_lVelY >>= 1;
	}
	s_ubJumpPrev = isJump;

	moveHorizontal();
	moveVertical();
	if(!s_ubDying) {
		animate();
	}
}

void playerUpdateSprite(void) {
	WORD wCamX = (WORD)s_pTileBuffer->pCamera->uPos.uwX;
	WORD wCamY = (WORD)s_pTileBuffer->pCamera->uPos.uwY;
	WORD wViewX = TO_PX(s_lPosX) - wCamX;
	WORD wViewY = TO_PX(s_lPosY) - wCamY;

	UBYTE isOn = 1;
	if(s_ubInvuln && (s_ubInvuln & 0x02)) {
		isOn = 0;
	}
	if(wViewX < -PLAYER_FRAME_W || wViewX > SCREEN_W) {
		isOn = 0;
	}
	if(wViewY > SCREEN_H) {
		isOn = 0;
	}
	if(wViewY < HUD_FOX_MIN_Y) {
		wViewY = HUD_FOX_MIN_Y;
	}

	if(
		s_pFoxFrames &&
		(s_ubDrawFrame != s_ubLastSprFrame || s_ubFacing != s_ubLastSprFacing)
	) {
		hudSetFoxFrame(foxFrame(s_ubFacing, s_ubDrawFrame));
		s_ubLastSprFrame = s_ubDrawFrame;
		s_ubLastSprFacing = s_ubFacing;
	}
	hudSetFoxPos(wViewX, wViewY, isOn);
}

void playerDestroy(void) {
	if(s_pFoxFrames) {
		memFree(s_pFoxFrames, FOX_FRAMES_BYTES);
		s_pFoxFrames = 0;
	}
}

UWORD playerGetCenterX(void) {
	return TO_PX(s_lPosX) + HB_X + HB_W / 2;
}

UWORD playerGetCenterY(void) {
	return TO_PX(s_lPosY) + HB_Y + HB_H / 2;
}

void playerGetHitbox(WORD *pwX, WORD *pwY, UWORD *puwW, UWORD *puwH) {
	*pwX = TO_PX(s_lPosX) + HB_X;
	*pwY = TO_PX(s_lPosY) + HB_Y;
	*puwW = HB_W;
	*puwH = HB_H;
}

UBYTE playerIsFalling(void) {
	return !s_isClimbing && !s_isSwinging && s_lVelY > 0;
}

UBYTE playerIsOnGround(void) {
	return s_isOnGround;
}

void playerBounceVel(LONG lVelY) {
	s_lVelY = lVelY;
	s_isOnGround = 0;
	s_ubCoyote = 0;
	leaveClimb();
	leaveSwing();
	platformsClearRide();
}

void playerBounce(void) {
	// Pop back up after landing on an enemy. Leaving the player airborne (and
	// clearing coyote) means the next jump must come from real ground.
	playerBounceVel(-PLAYER_STOMP_BOUNCE);
}

void playerTrampoline(void) {
	playerBounceVel(inputJump() ? -PLAYER_TRAMP_HELD : -PLAYER_TRAMP_BOUNCE);
	sfxPlayJump();
}

UBYTE playerGetHealth(void) {
	return s_ubHealth;
}

UBYTE playerHeal(void) {
	if(s_ubHealth && s_ubHealth < PLAYER_MAX_HEALTH) {
		++s_ubHealth;
		return 1;
	}
	return 0;
}

void playerHurt(WORD wSourceCenterX) {
	if(s_ubInvuln || s_ubDying || s_ubLocked || s_ubFrozen) {
		return; // still in i-frames, already dying, celebrating, or reading
	}
	if(s_ubHealth) {
		--s_ubHealth;
	}
	if(!s_ubHealth) {
		playerKill();
		return;
	}
	// Knock the player back away from the attacker and pop up slightly.
	WORD wCenterX = TO_PX(s_lPosX) + HB_X + HB_W / 2;
	s_lVelX = (wCenterX < wSourceCenterX) ? -PLAYER_KNOCKBACK_VX : PLAYER_KNOCKBACK_VX;
	s_lVelY = -PLAYER_KNOCKBACK_VY;
	s_isOnGround = 0;
	s_ubCoyote = 0;
	leaveClimb();
	leaveSwing();
	platformsClearRide();
	s_ubInvuln = PLAYER_INVULN_FRAMES;
	s_ubHurtLock = PLAYER_HURT_LOCK;
	sfxPlayHurt();
}

UBYTE playerConsumeDied(void) {
	UBYTE ubDied = s_ubDied;
	s_ubDied = 0;
	return ubDied;
}

UBYTE playerConsumeRespawned(void) {
	UBYTE ubRespawned = s_ubRespawned;
	s_ubRespawned = 0;
	return ubRespawned;
}

UBYTE playerGetFacing(void) {
	return s_ubFacing;
}

void playerGetSpitPos(WORD *pwX, WORD *pwY) {
	WORD wLeft = TO_PX(s_lPosX) + HB_X;
	*pwY = TO_PX(s_lPosY) + HB_Y + 8;
	*pwX = (s_ubFacing == PLAYER_FACE_RIGHT) ? (wLeft + HB_W) : wLeft;
}

UBYTE playerWantsFire(void) {
	return keyUse(KEY_CONTROL) || keyUse(KEY_Z) || joyUse(JOY1_FIRE);
}
