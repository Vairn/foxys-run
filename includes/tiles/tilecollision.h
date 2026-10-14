/* Auto-generated tile collision lookup table */

#ifndef _TILECOLLISION_H_
#define _TILECOLLISION_H_

#define COL_EMPTY  0
#define COL_SOLID  1
#define COL_ONEWAY 2
#define COL_HAZARD 3
#define COL_SLOPE  4
#define COL_LADDER 12
#define COL_ICE 13
#define COL_CONVEY_L 14
#define COL_CONVEY_R 15
#define COL_QUICKSAND 16
#define COL_CRUMBLE 17
#define COL_ONCE 18

#define TILE_SPECIAL_BASE 121
#define TILE_ICE_L 121
#define TILE_ICE_M 122
#define TILE_ICE_R 123
#define TILE_CONVEY_L_L 124
#define TILE_CONVEY_L_M 125
#define TILE_CONVEY_L_R 126
#define TILE_CONVEY_R_L 127
#define TILE_CONVEY_R_M 128
#define TILE_CONVEY_R_R 129
#define TILE_SAND_TOP 130
#define TILE_SAND_FILL 131
#define TILE_CRUMBLE_L 132
#define TILE_CRUMBLE_M 133
#define TILE_CRUMBLE_R 134
#define TILE_CRUMBLE_CRACK_L 135
#define TILE_CRUMBLE_CRACK_M 136
#define TILE_CRUMBLE_CRACK_R 137
#define TILE_ONCE_L 138
#define TILE_ONCE_M 139
#define TILE_ONCE_R 140
#define TILE_SPECIAL_COUNT 20

#define TILECOLLISION_COUNT 141

extern const unsigned char s_ubTileCollision[TILECOLLISION_COUNT];
/* Per-tile slope height from tile top (0..16) at each X. 16 = next tile. */
extern const unsigned char s_ubSlopeY[TILECOLLISION_COUNT][16];

#endif /* _TILECOLLISION_H_ */
