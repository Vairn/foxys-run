/* Shared level entity types (enemies / pickups / movers / switches / doors). */

#ifndef _LEVEL_TYPES_H_
#define _LEVEL_TYPES_H_

#include <ace/types.h>

#define ENEMY_TYPE_WALKER 0
#define ENEMY_TYPE_FLYER  1
#define ENEMY_TYPE_HOPPER 2
#define ENEMY_TYPE_BOSS   3
#define ENEMY_TYPE_BEETLE 4
#define ENEMY_TYPE_SLUG 5
#define ENEMY_TYPE_SLIMER 6
#define ENEMY_TYPE_DOG 7
#define ENEMY_TYPE_ANT 8
#define ENEMY_TYPE_DINO 9
#define ENEMY_TYPE_GATOR 10
#define ENEMY_TYPE_VULTURE 11
#define ENEMY_TYPE_BEE 12
#define ENEMY_TYPE_GRASSHOPPER 13
#define ENEMY_TYPE_PIRANHA 14
#define ENEMY_TYPE_BOSS_WOODS 15
#define ENEMY_TYPE_OPOSSUM ENEMY_TYPE_WALKER
#define ENEMY_TYPE_EAGLE   ENEMY_TYPE_FLYER
#define ENEMY_TYPE_FROG    ENEMY_TYPE_HOPPER

#define PICKUP_TYPE_CHERRY 0
#define PICKUP_TYPE_GEM 1
#define PICKUP_TYPE_CARROT 2
#define PICKUP_TYPE_STAR 3
#define PICKUP_TYPE_ACORN 4

#define SWITCH_FLAG_TOUCH  1
#define SWITCH_FLAG_TOGGLE 2
#define DOOR_KIND_VANISH  0
#define DOOR_KIND_SLIDE   1
#define DOOR_KIND_TERRAIN 2
#define SWITCH_CHANNEL_MAX 7

typedef struct {
	UWORD uwTileX, uwTileY;
	UBYTE ubType;
} tLevelEntity;

typedef struct {
	UWORD uwStartX, uwStartY;
	UWORD uwEndX, uwEndY;
	UWORD uwSpeed;
	UWORD uwChannel;
} tLevelPlatform;

#define VINE_LENGTH_MIN 3
#define VINE_LENGTH_MAX 6
#define VINE_LENGTH_DEFAULT 4

typedef struct {
	UWORD uwAnchorX, uwAnchorY;
	UWORD uwLength;
} tLevelVine;

typedef struct {
	UWORD uwTileX, uwTileY;
} tLevelTrampoline;

typedef struct {
	UWORD uwTileX, uwTileY;
	UWORD uwChannel;
	UWORD uwFlags;
} tLevelSwitch;

typedef struct {
	UWORD uwTileX, uwTileY;
	UWORD uwChannel;
	UWORD uwKind;
	UWORD uwEndX, uwEndY;
} tLevelDoor;

#endif /* _LEVEL_TYPES_H_ */
