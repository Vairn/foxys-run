#ifndef MONSTERS_H
#define MONSTERS_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * Level enemies for the current map.
 *
 * Behaviour slots (same numeric IDs in the .dat trailer):
 *   walker — ground walk; reverse at walls, ledges, and patrol edges
 *            (forest opossum / winter fox)
 *   flyer  — horizontal patrol + vertical bob
 *            (forest eagle / winter owl)
 *   hopper — idle then hop
 *            (forest frog / winter yeti)
 *   boss   — HP, chase/hop/throw, gem drop on death
 *   pack types 4+ — beetle/slug (armored walk), slimer (goo), ant (chase),
 *            dino/gator (charge), vulture/bee (swoop), grasshopper (leap),
 *            piranha (spit), woods boss (carrot throw)
 *
 * Lifecycle (shared bob manager):
 *   monstersLoad → bobManagerCreate → monstersInitBobs →
 *   bobReallocateBuffers → monstersProcess / monstersPushBobs →
 *   monstersDestroy
 */

void monstersLoad(tTileBufferManager *pTileBuffer);

void monstersInitBobs(void);

// Respawn every enemy at its spawn tile. Does not touch bitmaps or bob
// registrations — safe to call after the bob manager is already allocated.
void monstersReset(void);

void monstersProcess(void);

void monstersPushBobs(void);

void monstersDestroy(void);

UWORD monstersGetKilled(void);

// Damage the first live enemy whose hitbox overlaps the world-pixel rectangle.
// Returns 1 if something was hit. Does not bounce the player.
UBYTE monstersHitWorldRect(WORD wL, WORD wT, WORD wR, WORD wB);

UBYTE monstersBossAlive(void);
UBYTE monstersGetBossHp(void);
// 0 when the current map has no boss (HUD stays on gem/ammo).
UBYTE monstersGetBossHpMax(void);

#endif // MONSTERS_H
