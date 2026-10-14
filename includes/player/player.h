#ifndef PLAYER_H
#define PLAYER_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * SunnyLand player character (the "Foxy" fox).
 *
 * The player is drawn as four chained hardware sprites (32×32, 16-color
 * attached pairs) on HUD channels 0–3, multiplexed below the HUD overlay.
 * Physics is simple platformer (run, gravity, jump, ladders) with
 * axis-separated tile collision against the baked per-tile map
 * (see collision.h / collisionInit()).
 *
 * Lifecycle:
 *   playerLoad()             - load bitmaps from disk (before systemUnuse())
 *   bobManagerCreate(...)    - other entities still use bobs
 *   ...pickups/monsters/effects init + bobReallocateBuffers()
 *   playerReset(tx, ty)      - place at a spawn tile
 *   hudCreate(pView)         - must precede sprite updates (owns the channels)
 *   ...per frame: playerProcess() then playerUpdateSprite()
 *   playerDestroy()          - free bitmaps
 */

void playerLoad(tTileBufferManager *pTileBuffer);

void playerReset(UWORD uwSpawnTileX, UWORD uwSpawnTileY);

void playerProcess(void);

// Copy the current frame into the HUD sprite chain and place it in view space.
void playerUpdateSprite(void);

void playerDestroy(void);

UWORD playerGetCenterX(void);

UWORD playerGetCenterY(void);

// World-space hitbox (top-left + size, in pixels), used for entity overlap.
void playerGetHitbox(WORD *pwX, WORD *pwY, UWORD *puwW, UWORD *puwH);

// True while the player is moving downward (used for stomp detection).
UBYTE playerIsFalling(void);

// True while standing on a floor (tiles, mover, or door).
UBYTE playerIsOnGround(void);

// Launch the player upward after stomping an enemy.
void playerBounce(void);

// Launch with an explicit 8.8 velocity (negative is up). Clears climb/swing/ride.
void playerBounceVel(LONG lVelY);

// Trampoline launch: stronger bounce; hold jump for extra height.
void playerTrampoline(void);

UBYTE playerGetHealth(void);

// Damage the player from a hazard/enemy centred at wSourceCenterX (world px).
// No-op while invulnerable; drains health, knocks back, and grants i-frames.
// When health hits zero, plays the surprise anim then flags a death.
void playerHurt(WORD wSourceCenterX);

// Freeze input for the end-of-level card. Gravity still settles the fox.
void playerLock(void);

// Freeze input for a sign card. Keeps the current pose; gravity still settles.
void playerFreeze(void);

// Clear a sign freeze so the fox can move again.
void playerUnlock(void);

// Returns 1 (once) if the player ran out of health since the last call.
UBYTE playerConsumeDied(void);

// Returns 1 (once) if the player died and respawned since the last call,
// clearing the flag. Lets the caller redraw the whole map after the camera
// jumps back to the spawn point.
UBYTE playerConsumeRespawned(void);

#define PLAYER_START_HEALTH 3
#define PLAYER_MAX_HEALTH 5

// Restore one heart if the player is alive and not already at max health.
// Returns 1 if a heart was added.
UBYTE playerHeal(void);

#define PLAYER_FACE_RIGHT 0
#define PLAYER_FACE_LEFT  1

// 0 = facing right, 1 = facing left (PLAYER_FACE_*).
UBYTE playerGetFacing(void);

// World-pixel origin for a spit (around the snout).
void playerGetSpitPos(WORD *pwX, WORD *pwY);

// Edge-triggered fire: joystick fire, Left Ctrl, or Z. Consumes the press.
UBYTE playerWantsFire(void);

#endif // PLAYER_H
