/* Auto-generated tile collision lookup table */

#include "tiles/tilecollision.h"

const unsigned char s_ubTileCollision[TILECOLLISION_COUNT] = {
	0, 1, 1, 1, 1, 2, 2, 1, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 2, 2, 3, 4, 3, 4, 1, 1, 1, 2, 2, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 12, 0, 2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 2, 2, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0,
	13, 13, 13, 14, 14, 14, 15, 15, 15, 16, 16, 17, 17, 17, 17, 17, 17, 18, 18, 18
};

/* The old table was one 16-byte row per tile. Only ten tiles are slopes,
 * and those ten share five profiles. */
static const unsigned char s_ubSlopeY[][16] = {
	{16, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8},
	{8, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0},
	{0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8},
	{8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16}
};

static const unsigned char s_ubSlopeTile[] = {
	8, 9, 10, 11, 12, 13, 14, 15, 31, 33
};
static const unsigned char s_ubSlopeRow[] = {
	0, 1, 2, 3, 0, 1, 2, 3, 4, 4
};

const unsigned char *tileSlopeRow(unsigned uwTile) {
	unsigned i;

	for(i = 0; i < sizeof(s_ubSlopeTile); ++i) {
		if(s_ubSlopeTile[i] == uwTile) {
			return s_ubSlopeY[s_ubSlopeRow[i]];
		}
	}
	return 0;
}
