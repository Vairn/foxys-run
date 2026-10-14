#ifndef COLLISION_H
#define COLLISION_H

#include <ace/types.h>
#include <ace/managers/viewport/tilebuffer.h>
#include <tiles/tilecollision.h>
#include "world/level.h"

/**
 * Fast tile collision.
 *
 * loadMap() fills ACE's column-pointer tile grid (UWORD indices). Physics used
 * to chase those pointers and hit s_ubTileCollision[] on every probe.
 * collisionInit() bakes a 1-byte-per-tile map and a 16-px slope-height LUT so
 * the per-frame path is: bounds check, (x*height+y) byte load, optional LUT.
 *
 * Map cells are COL_EMPTY/SOLID/ONEWAY/HAZARD/LADDER, or COL_SLOPE+shapeId
 * for ramps. Shape IDs occupy COL_SLOPE .. COL_SLOPE+COL_SLOPE_SHAPES-1
 * (4..11). COL_LADDER is 12 so it never aliases a slope shape.
 * Walk-tops after that: ICE, CONVEY_L/R, CRUMBLE, ONCE. QUICKSAND is a volume.
 */

#define COL_TILE_SHIFT 4
#define COL_TILE_SIZE (1 << COL_TILE_SHIFT)
#define COL_SLOPE_SHAPES 8

extern UBYTE *g_ubColMap;
extern UBYTE g_ubColSlopeY[COL_SLOPE_SHAPES][COL_TILE_SIZE];
extern UWORD g_uwColWorldWPx;
extern UWORD g_uwColWorldHPx;
extern UWORD g_uwColTilesY;

void collisionInit(tTileBufferManager *pTileBuffer);
void collisionDestroy(void);

_Static_assert(
	COL_LADDER == COL_SLOPE + COL_SLOPE_SHAPES,
	"COL_LADDER must sit just after the packed slope-shape IDs"
);

static inline UBYTE colIsSlope(UBYTE ubCol) {
	return ubCol >= COL_SLOPE && ubCol < COL_SLOPE + COL_SLOPE_SHAPES;
}

static inline UBYTE colIsLadder(UBYTE ubCol) {
	return ubCol == COL_LADDER;
}

static inline UBYTE colIsWalkTop(UBYTE ubCol) {
	return (
		ubCol == COL_ONEWAY || ubCol == COL_ICE ||
		ubCol == COL_CONVEY_L || ubCol == COL_CONVEY_R ||
		ubCol == COL_CRUMBLE || ubCol == COL_ONCE
	);
}

static inline UBYTE colIsBlocking(UBYTE ubCol) {
	return ubCol == COL_SOLID || colIsSlope(ubCol);
}

// Pixel lookup. Out-of-bounds X is a wall; Y outside the map is empty (sky/pit).
static inline UBYTE colAt(WORD wPxX, WORD wPxY) {
	if((UWORD)wPxX >= g_uwColWorldWPx) {
		return COL_SOLID;
	}
	if((UWORD)wPxY >= g_uwColWorldHPx) {
		return COL_EMPTY; // sky above, pit below
	}
	UWORD uwTileX = (UWORD)wPxX >> COL_TILE_SHIFT;
	UWORD uwTileY = (UWORD)wPxY >> COL_TILE_SHIFT;
	return g_ubColMap[uwTileX * g_uwColTilesY + uwTileY];
}

// Walkable surface Y in the tile whose top is wTileTop. -1 if none.
static inline WORD colSurfaceYInTile(WORD wPxX, WORD wTileTop) {
	UBYTE ubCol = colAt(wPxX, wTileTop);
	if(ubCol == COL_SOLID) {
		return wTileTop;
	}
	if(colIsSlope(ubCol)) {
		return wTileTop + g_ubColSlopeY[ubCol - COL_SLOPE][wPxX & (COL_TILE_SIZE - 1)];
	}
	return -1;
}

// True if wAbove (surface in the tile above wTileTop) still supports probe
// row wY. Ramp heights are 0..16. Offset 16 is this tile's top (KaiN's toe).
// Offset 15 still counts for the first pixel of the empty tile below.
static inline UBYTE colFromTileAbove(WORD wAbove, WORD wY, WORD wTileTop) {
	if(wAbove < 0 || wY < wAbove) {
		return 0;
	}
	if(wAbove == wTileTop) {
		return 1;
	}
	return (UBYTE)(wAbove == wTileTop - 1 && wY <= wTileTop);
}

// Highest floor surface that contains probe row wY along [wX0,wX1]. -1 if none.
WORD colFloorHitY(WORD wY, WORD wX0, WORD wX1);

// Solid wall along a vertical span [wY0,wY1] at column x? Slopes are never
// walls. A solid at/below the walkable surface at this X (ramp, or a short
// step onto the flat) is treated as floor so climbing is not blocked.
UBYTE colBlockedVSpan(WORD wX, WORD wY0, WORD wY1);

// Solid ceiling along [wX0,wX1] at row y.
UBYTE colBlockedCeiling(WORD wY, WORD wX0, WORD wX1);

// One-way / ice / conveyor / crumble / once along [wX0,wX1] at row y.
UBYTE colOneWayHSpan(WORD wY, WORD wX0, WORD wX1);

// True if any tile along the vertical pixel span [wY0,wY1] at column x is a ladder.
UBYTE colLadderInSpan(WORD wX, WORD wY0, WORD wY1);

// Highest solid/slope surface under [wX0,wX1] whose Y is in [wYMin,wYMax].
WORD colFindSurfaceInRange(WORD wX0, WORD wX1, WORD wYMin, WORD wYMax);

// Sweep a falling foot path against solid/slope surfaces, then the landing row.
WORD colSweepFloor(WORD wOldBottom, WORD wBottom, WORD wX0, WORD wX1);

// True if pixel (wX,wY) is on/inside a solid or slope surface.
static inline UBYTE colHasFloor(WORD wX, WORD wY) {
	WORD wTop = wY & ~(COL_TILE_SIZE - 1);
	WORD wSurf = colSurfaceYInTile(wX, wTop);
	if(wSurf >= 0 && wY >= wSurf && wSurf <= wTop + COL_TILE_SIZE) {
		return 1;
	}
	if(wTop >= COL_TILE_SIZE) {
		WORD wAbove = colSurfaceYInTile(wX, wTop - COL_TILE_SIZE);
		if(colFromTileAbove(wAbove, wY, wTop)) {
			return 1;
		}
	}
	return 0;
}

// Grounded snap. Fast path: feet already rest on a flat solid/one-way tile top
// (two byte lookups). Slopes, embedding, and 1-tile step-downs take the slow
// path. Writes the surface Y (first solid pixel below the body). Returns 0 if
// there is no floor in range (walked off a ledge).
UBYTE colStickFeet(WORD wLeft, WORD wRight, WORD wFeet, WORD *pwSurf);

#endif /* COLLISION_H */
