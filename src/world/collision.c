/* Fast tile-collision map + shared span probes.
 *
 * See collision.h. Built once per level from the tilebuffer and the generated
 * compact-tileset tables in data/tiles/tilecollision.c.
 */
#include "world/collision.h"
#include "assets/assets.h"

#include "debug/serial_log.h"
#include <ace/managers/memory.h>
#include <string.h>

UBYTE *g_ubColMap;
UBYTE g_ubColSlopeY[COL_SLOPE_SHAPES][COL_TILE_SIZE];
UWORD g_uwColWorldWPx;
UWORD g_uwColWorldHPx;
UWORD g_uwColTilesY;
static ULONG s_ulColMapBytes;

void collisionDestroy(void) {
	if(g_ubColMap) {
		memFree(g_ubColMap, s_ulColMapBytes);
		g_ubColMap = 0;
	}
	s_ulColMapBytes = 0;
	g_uwColWorldWPx = 0;
	g_uwColWorldHPx = 0;
	g_uwColTilesY = 0;
}

void collisionInit(tTileBufferManager *pTileBuffer) {
	UWORD uwW = levelGetWidthTiles();
	UWORD uwH = levelGetHeightTiles();

	collisionDestroy();
	if(!uwW) {
		uwW = pTileBuffer->uTileBounds.uwX;
	}
	if(uwW > LEVEL_MAX_WIDTH_TILES) {
		uwW = LEVEL_MAX_WIDTH_TILES;
	}
	if(!uwW) {
		return;
	}

	if(!uwH) {
		uwH = LEVEL_SCREEN_HEIGHT_TILES;
	}
	if(uwH > LEVEL_MAX_HEIGHT_TILES) {
		uwH = LEVEL_MAX_HEIGHT_TILES;
	}
	s_ulColMapBytes = (ULONG)uwW * uwH;
	g_uwColTilesY = uwH;
	g_ubColMap = memAllocFastClear(s_ulColMapBytes);
	if(!g_ubColMap) {
		logWrite("ERR: colmap alloc %lu bytes\n", s_ulColMapBytes);
		s_ulColMapBytes = 0;
		g_uwColTilesY = 0;
		return;
	}

	g_uwColWorldWPx = uwW << COL_TILE_SHIFT;
	g_uwColWorldHPx = uwH << COL_TILE_SHIFT;

	UBYTE ubIdxToCol[LEVEL_MAX_TERRAIN_TILES];
	UWORD uwColCount = TILECOLLISION_COUNT;
	UBYTE ubShapes = 0;

	for(UWORD i = 0; i < LEVEL_MAX_TERRAIN_TILES; ++i) {
		ubIdxToCol[i] = COL_EMPTY;
	}

	const tLevelDef *pLevel = levelGetCurrent();
	const char *szColPath = (pLevel) ? pLevel->szColPath : "";
	if(szColPath[0] && sunnyFileExists(szColPath)) {
		tFile *pCol = sunnyOpenFile(szColPath);
		if(pCol) {
			UWORD uwN = sunnyReadWord(pCol);
			if(uwN > LEVEL_MAX_TERRAIN_TILES) {
				uwN = LEVEL_MAX_TERRAIN_TILES;
			}
			uwColCount = uwN;
			fileReadBytes(pCol, ubIdxToCol, uwN);
			fileClose(pCol);
			logWrite("Collision LUT: %s (%u tiles)\n", szColPath, uwN);
		}
	}
	else for(UWORD i = 0; i < TILECOLLISION_COUNT; ++i) {
		UBYTE ubType = s_ubTileCollision[i];
		if(ubType != COL_SLOPE) {
			ubIdxToCol[i] = ubType;
			continue;
		}
		UBYTE s;
		for(s = 0; s < ubShapes; ++s) {
			if(!memcmp(g_ubColSlopeY[s], s_ubSlopeY[i], COL_TILE_SIZE)) {
				break;
			}
		}
		if(s == ubShapes) {
			if(ubShapes >= COL_SLOPE_SHAPES) {
				ubIdxToCol[i] = COL_SOLID;
				continue;
			}
			memcpy(g_ubColSlopeY[s], s_ubSlopeY[i], COL_TILE_SIZE);
			++ubShapes;
		}
		ubIdxToCol[i] = (UBYTE)(COL_SLOPE + s);
	}

	for(UWORD x = 0; x < uwW; ++x) {
		UWORD uwCol = (UWORD)(x * uwH);
		for(UWORD y = 0; y < uwH; ++y) {
			tTileBufferTileIndex uwIdx = pTileBuffer->pTileData[x][y];
			g_ubColMap[uwCol + y] = (
				(ULONG)uwIdx < uwColCount
			) ? ubIdxToCol[uwIdx] : COL_EMPTY;
		}
	}
}

WORD colFloorHitY(WORD wY, WORD wX0, WORD wX1) {
	WORD wBest = 32767;
	WORD wTileTop = wY & ~(COL_TILE_SIZE - 1);
	WORD wX = wX0;
	for(;;) {
		WORD wSurf = colSurfaceYInTile(wX, wTileTop);
		if(
			wSurf >= 0 && wY >= wSurf &&
			wSurf <= wTileTop + COL_TILE_SIZE && wSurf < wBest
		) {
			wBest = wSurf;
		}
		if(wTileTop >= COL_TILE_SIZE) {
			WORD wAbove = colSurfaceYInTile(wX, wTileTop - COL_TILE_SIZE);
			if(colFromTileAbove(wAbove, wY, wTileTop) && wAbove < wBest) {
				wBest = wAbove;
			}
		}
		if(wX >= wX1) {
			break;
		}
		wX += COL_TILE_SIZE;
		if(wX > wX1) {
			wX = wX1;
		}
	}
	return (wBest == 32767) ? -1 : wBest;
}

UBYTE colBlockedVSpan(WORD wX, WORD wY0, WORD wY1) {
	// Dirt fill and the flat beyond a ramp sit in the body AABB at the old
	// (lower) Y while climbing. Treating them as walls snaps X back — or,
	// at a tile seam, into empty space — so the uphill falls through.
	// A solid at/below the walkable surface here is a floor, not a wall.
	WORD wFloor = colFindSurfaceInRange(
		wX, wX, wY1 - COL_TILE_SIZE, wY1 + 1
	);
	WORD wScanEnd = wY1;
	if(wFloor >= 0) {
		UBYTE ubFloor = colAt(wX, wFloor);
		if(colIsSlope(ubFloor) || (wY1 - wFloor) <= (COL_TILE_SIZE / 2)) {
			wScanEnd = wFloor - 1;
			if(wY0 > wScanEnd) {
				return 0;
			}
		}
	}

	WORD wY = wY0;
	for(;;) {
		if(colAt(wX, wY) == COL_SOLID) {
			return 1;
		}
		if(wY >= wScanEnd) {
			break;
		}
		wY += COL_TILE_SIZE;
		if(wY > wScanEnd) {
			wY = wScanEnd;
		}
	}
	return 0;
}

UBYTE colBlockedCeiling(WORD wY, WORD wX0, WORD wX1) {
	WORD wX = wX0;
	for(;;) {
		if(colAt(wX, wY) == COL_SOLID) {
			return 1;
		}
		if(wX >= wX1) {
			break;
		}
		wX += COL_TILE_SIZE;
		if(wX > wX1) {
			wX = wX1;
		}
	}
	return 0;
}

UBYTE colLadderInSpan(WORD wX, WORD wY0, WORD wY1) {
	if(wY0 > wY1) {
		WORD wSwap = wY0;
		wY0 = wY1;
		wY1 = wSwap;
	}
	WORD wY = wY0;
	for(;;) {
		if(colIsLadder(colAt(wX, wY))) {
			return 1;
		}
		if(wY >= wY1) {
			break;
		}
		wY += COL_TILE_SIZE;
		if(wY > wY1) {
			wY = wY1;
		}
	}
	return 0;
}

UBYTE colOneWayHSpan(WORD wY, WORD wX0, WORD wX1) {
	WORD wX = wX0;
	for(;;) {
		if(colIsWalkTop(colAt(wX, wY))) {
			return 1;
		}
		if(wX >= wX1) {
			break;
		}
		wX += COL_TILE_SIZE;
		if(wX > wX1) {
			wX = wX1;
		}
	}
	return 0;
}

WORD colFindSurfaceInRange(WORD wX0, WORD wX1, WORD wYMin, WORD wYMax) {
	WORD wBest = 32767;
	WORD wRowTop = (wYMin - (COL_TILE_SIZE - 1));
	if(wRowTop < 0) {
		wRowTop = 0;
	}
	wRowTop &= ~(COL_TILE_SIZE - 1);
	WORD wLast = wYMax & ~(COL_TILE_SIZE - 1);
	for(;;) {
		WORD wX = wX0;
		for(;;) {
			WORD wSurf = colSurfaceYInTile(wX, wRowTop);
			if(
				wSurf >= wYMin && wSurf <= wYMax &&
				wSurf <= wRowTop + COL_TILE_SIZE && wSurf < wBest
			) {
				wBest = wSurf;
			}
			if(wX >= wX1) {
				break;
			}
			wX += COL_TILE_SIZE;
			if(wX > wX1) {
				wX = wX1;
			}
		}
		if(wRowTop >= wLast) {
			break;
		}
		wRowTop += COL_TILE_SIZE;
	}
	return (wBest == 32767) ? -1 : wBest;
}

WORD colSweepFloor(WORD wOldBottom, WORD wBottom, WORD wX0, WORD wX1) {
	WORD wHit = -1;
	// Include the tile that still contains the old feet. Starting at
	// oldBottom+1 skipped a slope whose surface is the last pixel of that
	// tile, and the empty tile below does not see it (heights are 0..16).
	WORD wRow = wOldBottom & ~(COL_TILE_SIZE - 1);
	if(wRow < 0) {
		wRow = 0;
	}
	WORD wLastRow = wBottom & ~(COL_TILE_SIZE - 1);
	for(;;) {
		WORD wX = wX0;
		for(;;) {
			WORD wSurf = colSurfaceYInTile(wX, wRow);
			if(
				wSurf >= 0 && wSurf <= wRow + COL_TILE_SIZE &&
				wBottom >= wSurf && wOldBottom < wBottom && wOldBottom <= wSurf
			) {
				if(wHit < 0 || wSurf < wHit) {
					wHit = wSurf;
				}
			}
			if(wX >= wX1) {
				break;
			}
			wX += COL_TILE_SIZE;
			if(wX > wX1) {
				wX = wX1;
			}
		}
		if(wRow >= wLastRow) {
			break;
		}
		wRow += COL_TILE_SIZE;
	}
	if(wHit < 0) {
		wHit = colFloorHitY(wBottom, wX0, wX1);
	}
	return wHit;
}

UBYTE colStickFeet(WORD wLeft, WORD wRight, WORD wFeet, WORD *pwSurf) {
	WORD wBelow = wFeet + 1;
	// Common case: feet resting clear on the top pixel of a flat solid/one-way
	// tile. Both range scans below agree on wBelow here, so skip them. The feet
	// row itself must be clear of floor geometry — otherwise this is the
	// embedded case and the surface may be higher up. Only valid while the span
	// fits in two tiles, so these two columns cover it (monsters are wider).
	if(
		wRight - wLeft < COL_TILE_SIZE &&
		(wBelow & (COL_TILE_SIZE - 1)) == 0
	) {
		UBYTE ubL = colAt(wLeft, wBelow);
		UBYTE ubR = colAt(wRight, wBelow);
		if(
			!colIsSlope(ubL) && !colIsSlope(ubR) && (
				ubL == COL_SOLID || colIsWalkTop(ubL) ||
				ubR == COL_SOLID || colIsWalkTop(ubR)
			) &&
			!colHasFloor(wLeft, wFeet) && !colHasFloor(wRight, wFeet)
		) {
			*pwSurf = wBelow;
			return 1;
		}
	}

	WORD wSurf;
	if(colFloorHitY(wFeet, wLeft, wRight) >= 0) {
		// Embedded (or resting on a bottom-edge boundary): depenetrate up to the
		// highest surface within a tile of the feet. Scanning the band matters
		// on ramps — at the top of one, the far foot is over a flat tile a row
		// higher, and that surface does not contain the feet row. Use a full
		// tile (not TILE-1) so a feet row on the first pixel of the tile below
		// still sees the upper ramp (surface at offset 0).
		wSurf = colFindSurfaceInRange(
			wLeft, wRight, wFeet - COL_TILE_SIZE, wBelow
		);
	}
	else {
		// Air just below the feet: step down onto a nearby ramp/floor.
		wSurf = colFindSurfaceInRange(
			wLeft, wRight, wBelow, wFeet + COL_TILE_SIZE
		);
	}
	if(wSurf < 0) {
		return 0;
	}
	*pwSurf = wSurf;
	return 1;
}
