/* Play state: tilebuffer, camera, draw-then-simulate loop. */
#include "game/gamestate.h"
#include "world/level.h"
#include "player/player.h"
#include "entities/pickups.h"
#include "entities/monsters.h"
#include "entities/effects.h"
#include "ui/hud.h"
#include "entities/shots.h"
#include "game/screens.h"
#include "game/session.h"
#include "ui/overlay.h"
#include "props/signs.h"
#include "world/collision.h"
#include "props/platforms.h"
#include "props/trampolines.h"
#include "props/vines.h"
#include "props/switches.h"
#include "props/doors.h"
#include "world/terrain.h"
#include "audio/music.h"
#include "world/sky.h"
#include "game/loading.h"
#include <ace/managers/system.h>
#include <ace/managers/game.h>
#include <ace/managers/memory.h>
#include <ace/managers/bob.h>
#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/viewport/tilebuffer.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/utils/file.h>
#include <ace/utils/bitmap.h>
#include <hardware/dmabits.h>
#include <string.h>
#include "assets/assets.h"
#include "game/game_defs.h"
#include "debug/serial_log.h"

extern tStateManager *g_pStateManager;
extern tState *g_pGameState;
extern tState *g_pGameOverState;
extern tState *g_pGameWonState;

#define GAME_BPP 4
#define WORLD_TILES_X LEVEL_MAX_WIDTH_TILES
static UWORD s_uwWorldTilesX;
static UWORD s_uwWorldTilesY;

// Decoration layers composited over terrain: index 0 = empty cell (skipped).
// Layer 0 draws behind the player, layer 1 in front (see onTileDraw note).
#define PROP_LAYER_COUNT 2

static tView *s_pView;
static tVPort *s_pVPort;
static tTileBufferManager *s_pTileBuffer;
static tBitMap *s_pTiles;          // terrain tileset (drawn opaque by tilebuffer)
static tBitMap *s_pPropTiles;      // pixel-accurate props tileset
static tBitMap *s_pPropTilesMask;  // interleaved mask for the props tileset
static tBitMap *s_pPristine;       // clean playfield for ACE_BOB_PRISTINE_BUFFER
static UWORD s_pPalette[16];
static UBYTE s_ubRetryRedraw;
static UBYTE s_ubFadeIn;
static UBYTE s_ubGfxDumpFrame;

static void gfxDump(const char *szTag) {
	const tBitMap *pBm;
	const tBitMap *pTs;
	UWORD uwTx, uwTy, i;
	UWORD uwTile, uwSrcY, uwDstW;
	const volatile UWORD *pChip;

	/* Do not read $DFF custom write-only regs (BPLCON, FMODE, BPLPT, DDF).
	 * Those reads are open-bus and can strobe Lisa/copper on AGA. */
	logWrite(
		"GFX %s os=%u blitOs=%u\n",
		szTag, systemIsUsed(), systemBlitterIsReleasedToOs()
	);
	if(!s_pTileBuffer || !s_pTileBuffer->pScroll) {
		return;
	}
	pBm = s_pTileBuffer->pScroll->pBack;
	pTs = s_pTiles;
	if(pBm) {
		uwDstW = (UWORD)(pBm->BytesPerRow << 3);
		if(bitmapIsInterleaved(pBm) && pBm->Depth) {
			uwDstW = (UWORD)(uwDstW / pBm->Depth);
		}
		logWrite(
			"  scroll chip=%u intl=%u %ux%ux%u cam=%u,%u bounds=%ux%u\n",
			bitmapIsChip(pBm), bitmapIsInterleaved(pBm),
			uwDstW, pBm->Rows, pBm->Depth,
			s_pTileBuffer->pCamera->uPos.uwX,
			s_pTileBuffer->pCamera->uPos.uwY,
			s_pTileBuffer->uTileBounds.uwX,
			s_pTileBuffer->uTileBounds.uwY
		);
	}
	if(pTs) {
		logWrite(
			"  tileset chip=%u intl=%u %ux%ux%u\n",
			bitmapIsChip(pTs), bitmapIsInterleaved(pTs),
			(UWORD)((pTs->BytesPerRow << 3) / (bitmapIsInterleaved(pTs) && pTs->Depth ? pTs->Depth : 1)),
			pTs->Rows, pTs->Depth
		);
	}
	if(!s_pTileBuffer->pTileData) {
		return;
	}
	uwTx = (UWORD)(s_pTileBuffer->pCamera->uPos.uwX >> TILE_SHIFT);
	uwTy = levelGetSpawnTileY();
	if(uwTy >= s_pTileBuffer->uTileBounds.uwY) {
		uwTy = 0;
	}
	logWrite("  tiles y=%u x=%u:", uwTy, uwTx);
	uwTile = 0;
	for(i = 0; i < 8; ++i) {
		UWORD uwX = (UWORD)(uwTx + i);
		if(uwX < s_pTileBuffer->uTileBounds.uwX) {
			UWORD uwId = s_pTileBuffer->pTileData[uwX][uwTy];
			if(!uwTile && uwId) {
				uwTile = uwId;
			}
			logWrite(" %u", uwId);
		}
	}
	logWrite("\n");
	if(pTs && uwTile) {
		uwSrcY = (UWORD)(uwTile << TILE_SHIFT);
		if((UWORD)(uwSrcY + TILE_SIZE) > pTs->Rows) {
			logWrite(
				"  ERR: tile %u srcY=%u overflows tileset rows %u\n",
				uwTile, uwSrcY, pTs->Rows
			);
		}
	}
	if(pBm && pBm->Planes[0] && uwTy < (pBm->Rows >> TILE_SHIFT)) {
		pChip = (const volatile UWORD *)(
			(const UBYTE *)pBm->Planes[0] +
			(ULONG)pBm->BytesPerRow * ((ULONG)uwTy << TILE_SHIFT)
		);
		logWrite("  chip:");
		for(i = 0; i < 8; ++i) {
			logWrite(" %04X", pChip[i]);
		}
		logWrite("\n");
	}
	if(pTs && pTs->Planes[0] && uwTile) {
		uwSrcY = (UWORD)(uwTile << TILE_SHIFT);
		if((UWORD)(uwSrcY + TILE_SIZE) <= pTs->Rows) {
			pChip = (const volatile UWORD *)(
				(const UBYTE *)pTs->Planes[0] +
				(ULONG)pTs->BytesPerRow * uwSrcY
			);
			logWrite("  src%u:", uwTile);
			for(i = 0; i < 8; ++i) {
				logWrite(" %04X", pChip[i]);
			}
			logWrite("\n");
		}
	}
}

// Prop layers, column-major to match the tilebuffer: [tileX * height + tileY].
// Allocated for the authored size in gameGsCreate, freed on teardown so the
// title is not paying for two 360-wide maps.
static tTileBufferTileIndex *s_pPropBack;
static tTileBufferTileIndex *s_pPropFront;
static ULONG s_ulPropBytes;
static UWORD s_uwPropTilesX;
static UWORD s_uwPropTilesY;

static void propsDestroy(void) {
	if(s_pPropBack) {
		memFree(s_pPropBack, s_ulPropBytes);
		s_pPropBack = 0;
	}
	if(s_pPropFront) {
		memFree(s_pPropFront, s_ulPropBytes);
		s_pPropFront = 0;
	}
	s_ulPropBytes = 0;
	s_uwPropTilesX = 0;
	s_uwPropTilesY = 0;
}

static UBYTE propsCreate(UWORD uwTilesX, UWORD uwTilesY) {
	propsDestroy();
	if(!uwTilesX || !uwTilesY) {
		return 0;
	}
	s_ulPropBytes = (ULONG)uwTilesX * uwTilesY * sizeof(tTileBufferTileIndex);
	s_pPropBack = memAllocFastClear(s_ulPropBytes);
	s_pPropFront = memAllocFastClear(s_ulPropBytes);
	if(!s_pPropBack || !s_pPropFront) {
		logWrite("ERR: prop layer alloc %lu bytes\n", s_ulPropBytes);
		propsDestroy();
		return 0;
	}
	s_uwPropTilesX = uwTilesX;
	s_uwPropTilesY = uwTilesY;
	return 1;
}

// Blit one prop tile over the terrain tile just written to the scroll buffer.
// Uses a masked (cookie-cut) blit so index-0 pixels leave terrain untouched.
// Must use the UNSAFE masked blit here: when the tilebuffer scrolls in X it
// deliberately hands us a buffer X that overflows the bitmap width (drawing
// onto the next interleaved bitplane as if it were bitplane 0 — see
// tileBufferDrawTile in ACE). The bounds-checked blitCopyMask rejects that as
// an "ILLEGAL BLIT Dest out of range" under ACE_DEBUG, exactly like the
// terrain draw would if it didn't use blitUnsafeCopyAligned.
static inline void drawPropTile(
	tTileBufferTileIndex uwTile, tBitMap *pBitMap, UWORD uwBitMapX, UWORD uwBitMapY
) {
	blitUnsafeCopyMask(
		s_pPropTiles, 0, uwTile << TILE_SHIFT,
		pBitMap, uwBitMapX, uwBitMapY,
		TILE_SIZE, TILE_SIZE,
		(const UBYTE*)s_pPropTilesMask->Planes[0]
	);
}

static void onTileDraw(
	UWORD uwTileX, UWORD uwTileY,
	tBitMap *pBitMap, UWORD uwBitMapX, UWORD uwBitMapY
) {
	// Composite prop layers on top of the terrain tile ACE just drew. This
	// runs for every (re)drawn tile, so scrolling is handled automatically.
	// Compute the cell index once and take a single combined early-out: most
	// cells are empty in both layers, so this skips two blits' worth of setup
	// with one branch. Non-empty cells draw only the layer(s) actually present.
	// NOTE: both layers draw behind moving entities because the callback fires
	// before bobs are pushed. The player is a hardware sprite, so it sits in
	// front of the whole playfield (props included) regardless.
	if(s_pPropTiles && s_pPropBack && uwTileX < s_uwPropTilesX &&
	   uwTileY < s_uwPropTilesY) {
		UWORD uwCell = uwTileX * s_uwPropTilesY + uwTileY;
		tTileBufferTileIndex uwBack = s_pPropBack[uwCell];
		tTileBufferTileIndex uwFront = s_pPropFront[uwCell];
		if(uwBack | uwFront) {
			if(uwBack) {
				drawPropTile(uwBack, pBitMap, uwBitMapX, uwBitMapY);
			}
			if(uwFront) {
				drawPropTile(uwFront, pBitMap, uwBitMapX, uwBitMapY);
			}
		}
	}
	// ACE restores BOBs from this copy. Dest X can overflow the bitmap width
	// on a wrap column — same trick as the terrain/prop stamp.
	if(s_pPristine) {
		blitUnsafeCopyAligned(
			pBitMap, uwBitMapX, uwBitMapY,
			s_pPristine, uwBitMapX, uwBitMapY,
			TILE_SIZE, TILE_SIZE
		);
	}
}

static void loadMap(void) {
	const tLevelDef *pLevel = levelGetCurrent();
	if(!pLevel) {
		logWrite("ERROR: Could not open map\n");
		return;
	}
	logWrite("Loading %s...\n", pLevel->szMapPath);
	tFile *pFile = sunnyOpenFile(pLevel->szMapPath);
	if (!pFile) {
		logWrite("ERROR: Could not open map\n");
		return;
	}

	UWORD uwFileW = sunnyReadWord(pFile);
	UWORD uwFileH = sunnyReadWord(pFile);
	logWrite(
		"Map size: %u x %u tiles (level %u)\n",
		(UWORD)uwFileW, (UWORD)uwFileH, (UWORD)levelGetIndex()
	);

	UWORD uwMapW = uwFileW;
	UWORD uwMapH = uwFileH;
	if(uwMapW > s_uwWorldTilesX) {
		uwMapW = s_uwWorldTilesX;
	}
	if(uwMapH > s_uwWorldTilesY) {
		uwMapH = s_uwWorldTilesY;
	}
	levelSetWidthTiles(uwMapW);
	levelSetHeightTiles(uwMapH);

	// Column-major tiles, then skip any file columns/rows we truncated so the
	// entity trailer stays aligned.
	for(UWORD x = 0; x < uwFileW; ++x) {
		if(x < uwMapW) {
			sunnyReadTiles(pFile, s_pTileBuffer->pTileData[x], uwMapH);
			if(uwFileH > uwMapH) {
				fileSeek(
					pFile,
					(uwFileH - uwMapH) * (LONG)sizeof(tTileBufferTileIndex),
					FILE_SEEK_CURRENT
				);
			}
		}
		else {
			fileSeek(
				pFile,
				uwFileH * (LONG)sizeof(tTileBufferTileIndex),
				FILE_SEEK_CURRENT
			);
		}
	}

	// Spawn / enemies / pickups trailer (same .dat file).
	levelLoadEntities(pFile);
	fileClose(pFile);

	// Clear unused columns when the map is shorter than the tilebuffer bound.
	for(UWORD x = uwMapW; x < s_uwWorldTilesX; ++x) {
		memset(
			s_pTileBuffer->pTileData[x], 0,
			s_uwWorldTilesY * sizeof(tTileBufferTileIndex)
		);
	}
	logWrite("Level loaded.\n");
}

static void loadProps(void) {
	const tLevelDef *pLevel = levelGetCurrent();
	if(!pLevel || !s_pPropBack || !s_pPropFront) {
		return;
	}
	logWrite("Loading %s...\n", pLevel->szPropsPath);

	tFile *pFile = sunnyOpenFile(pLevel->szPropsPath);
	if(!pFile) {
		logWrite("ERROR: Could not open props\n");
		return;
	}

	UWORD uwW = sunnyReadWord(pFile);
	UWORD uwFileH = sunnyReadWord(pFile);
	UWORD uwLayers = sunnyReadWord(pFile);
	logWrite("Props: %u x %u, %u layers\n", uwW, uwFileH, uwLayers);

	if(uwW > s_uwPropTilesX) {
		uwW = s_uwPropTilesX;
	}
	UWORD uwH = uwFileH;
	if(uwH > s_uwPropTilesY) {
		uwH = s_uwPropTilesY;
	}

	tTileBufferTileIndex *pLayers[PROP_LAYER_COUNT] = { s_pPropBack, s_pPropFront };
	UWORD uwToRead = (uwLayers < PROP_LAYER_COUNT) ? uwLayers : PROP_LAYER_COUNT;
	for(UWORD l = 0; l < uwToRead; ++l) {
		for(UWORD x = 0; x < uwW; ++x) {
			sunnyReadTiles(pFile, &pLayers[l][x * s_uwPropTilesY], uwH);
			if(uwFileH > uwH) {
				fileSeek(
					pFile,
					(LONG)(uwFileH - uwH) * (LONG)sizeof(tTileBufferTileIndex),
					FILE_SEEK_CURRENT
				);
			}
		}
	}
	fileClose(pFile);
	logWrite("Props loaded.\n");
}

// Center the camera on the player, clamped to the active level width.
static void cameraFollowPlayer(void) {
	WORD wVpW = s_pVPort->uwWidth;
	WORD wVpH = s_pVPort->uwHeight;
	LONG lWorldW = (LONG)levelGetWidthPx();
	LONG lMaxX = (lWorldW > wVpW) ? (lWorldW - wVpW) : 0;
	LONG lWorldH = (LONG)levelGetHeightPx();
	LONG lMaxY = (lWorldH > wVpH) ? (lWorldH - wVpH) : 0;

	LONG lCamX = (LONG)playerGetCenterX() - (wVpW >> 1);
	LONG lCamY = (LONG)playerGetCenterY() - (wVpH >> 1);
	if(lCamX < 0) { lCamX = 0; } else if(lCamX > lMaxX) { lCamX = lMaxX; }
	if(lCamY < 0) { lCamY = 0; } else if(lCamY > lMaxY) { lCamY = lMaxY; }

	cameraSetCoord(s_pTileBuffer->pCamera, (UWORD)lCamX, (UWORD)lCamY);
}

static void gameFadeOut(void) {
	skyDisable();
	hudSetFoxPos(0, 0, 0);
	hudProcess();
	systemSetDmaBit(DMAB_SPRITE, 0);
	sunnyFadeOut(s_pPalette, s_pVPort);
}

static void gameFadeIn(void) {
	sunnyFadeIn(s_pPalette, s_pVPort);
	skyEnable();
	systemSetDmaBit(DMAB_SPRITE, 1);
	copProcessBlocks();
}

// Replay the current map without tearing down the view or re-reading disk.
// Terrain gates restore tiles on doorsReset; retry then redraws the buffer.
static void gameRestartCurrentLevel(void) {
	gameFadeOut();
	sessionBeginLevel();
	pickupsReset();
	monstersReset();
	effectsReset();
	platformsReset();
	vinesReset();
	trampolinesReset();
	switchesReset();
	doorsReset();
	terrainReset();
	shotsReset();
	playerReset(levelGetSpawnTileX(), levelGetSpawnTileY());
	cameraFollowPlayer();
	hudUpdate();
	s_ubRetryRedraw = 1;
	s_ubFadeIn = 1;
}

static void gameLeave(tState *pNext) {
	if(pNext == g_pGameState) {
		musicKeepPlaying();
	}
	gameFadeOut();
	stateChange(g_pStateManager, pNext);
}

#define LOAD_STEPS 12

static UBYTE s_ubLoadStep;

static void loadTick(void) {
	loadingProgress(++s_ubLoadStep, LOAD_STEPS);
}

void gameGsCreate(void) {
	// Title card stays on the CRT while tiles, map, and music come off disk.
	// Peek winter/forest first so the card can pick its palette.
	// Stay in systemUse (destroy just took the OS) so viewLoad/IO cannot hang
	// on a blank copper list; ptplayer samples keep DMA-looping until Unuse.
	levelPeekMonsterSet();
	s_ubLoadStep = 0;
	loadingShow();

	// Drop title samples before the scroll / BOB peak. Same track is a no-op.
	musicStartForLevel();

	// Create the view and a single full-screen playfield viewport. The HUD is
	// drawn on top with hardware sprites, so it needs no viewport of its own.
	s_pView = viewCreate(0, TAG_DONE);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, GAME_BPP,
		TAG_DONE
	);
	loadTick();

	// Terrain tileset only — tileBufferCreate needs it. The ~100 KB props
	// sheet is loaded after bobReallocateBuffers; create/undraw do not.
	{
		const tLevelDef *pLevel = levelGetCurrent();
		logWrite("Terrain tileset: %s\n", pLevel->szTilesetPath);
		s_pTiles = sunnyLoadBitmap(pLevel->szTilesetPath, 0);
		s_pPropTiles = 0;
		s_pPropTilesMask = 0;
	}
	loadTick();

	UWORD uwTileCount = 1;
	if(s_pTiles && s_pTiles->Rows >= TILE_SIZE) {
		uwTileCount = (UWORD)(s_pTiles->Rows / TILE_SIZE);
		if(uwTileCount > LEVEL_MAX_TERRAIN_TILES) {
			uwTileCount = LEVEL_MAX_TERRAIN_TILES;
		}
	}

	{
		UWORD uwPeekW = 20, uwPeekH = LEVEL_SCREEN_HEIGHT_TILES;
		levelPeekMapSize(&uwPeekW, &uwPeekH);
		s_uwWorldTilesX = uwPeekW;
		s_uwWorldTilesY = uwPeekH;
		if(s_uwWorldTilesX < 20) {
			s_uwWorldTilesX = 20;
		}
		if(s_uwWorldTilesY < LEVEL_SCREEN_HEIGHT_TILES) {
			s_uwWorldTilesY = LEVEL_SCREEN_HEIGHT_TILES;
		}
	}
	// Tall maps pay Y-margin chip; 15-tall maps keep the short window.
	s_pTileBuffer = tileBufferCreate(0,
		TAG_TILEBUFFER_VPORT, s_pVPort,
		TAG_TILEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_TILEBUFFER_BOUND_TILE_X, s_uwWorldTilesX,
		TAG_TILEBUFFER_BOUND_TILE_Y, s_uwWorldTilesY,
		TAG_TILEBUFFER_Y_MARGIN,
		(s_uwWorldTilesY > LEVEL_SCREEN_HEIGHT_TILES) ? 2 : 0,
		TAG_TILEBUFFER_IS_DBLBUF, 1,
		TAG_TILEBUFFER_TILE_SHIFT, TILE_SHIFT,
		TAG_TILEBUFFER_REDRAW_QUEUE_LENGTH, 200,
		TAG_TILEBUFFER_CALLBACK_TILE_DRAW, onTileDraw,
		TAG_TILEBUFFER_TILESET, s_pTiles,
		TAG_TILEBUFFER_MAX_TILESET_SIZE, uwTileCount,
		TAG_DONE
	);
	loadTick();

	// Clean playfield copy for BOB restore. Same size as the scroll bitmap.
	s_pPristine = 0;
	if(s_pTileBuffer && s_pTileBuffer->pScroll && s_pTileBuffer->pScroll->pBack) {
		tBitMap *pBack = s_pTileBuffer->pScroll->pBack;
		UWORD uwWidth = bitmapGetByteWidth(pBack) << 3;
		s_pPristine = bitmapCreate(
			uwWidth, pBack->Rows, pBack->Depth,
			BMF_CLEAR | BMF_INTERLEAVED
		);
		if(!s_pPristine) {
			logWrite("ERR: pristine buffer alloc failed\n");
		}
	}

	// Load and apply this level's 16-colour palette
	{
		const tLevelDef *pLevel = levelGetCurrent();
		logWrite("Palette: %s\n", pLevel->szPalettePath);
		sunnyLoadPalette(pLevel->szPalettePath, s_pPalette, 16);
	}
	memcpy(s_pVPort->pPalette, s_pPalette, sizeof(s_pPalette));

	// Create copper sky gradient aligned to the (now full-screen) playfield.
	skyCreate(s_pView, s_pVPort);
	loadTick();

	// Load tile map + prop layers into memory
	loadMap();
	propsCreate(levelGetWidthTiles(), levelGetHeightTiles());
	collisionInit(s_pTileBuffer);
	terrainInit(s_pTileBuffer);
	loadTick();
	loadProps();
	loadTick();

	// Player bitmaps (disk IO). Other entities still use the shared bob
	// manager; the fox is a hardware sprite chained on the HUD.
	playerLoad(s_pTileBuffer);
	pickupsLoad(s_pTileBuffer);
	loadTick();
	monstersLoad(s_pTileBuffer);
	effectsLoad(s_pTileBuffer);
	loadTick();
	platformsLoad(s_pTileBuffer);
	vinesLoad(s_pTileBuffer);
	trampolinesLoad(s_pTileBuffer);
	loadTick();
	switchesLoad(s_pTileBuffer);
	doorsLoad(s_pTileBuffer);
	loadTick();
	// Restore from the pristine playfield instead of per-BOB save stacks.
	bobManagerCreate(
		s_pTileBuffer->pScroll->pFront,
		s_pTileBuffer->pScroll->pBack,
		s_pPristine,
		s_pTileBuffer->pScroll->uwBmAvailHeight
	);
	sessionBeginLevel();
	pickupsInitBobs();
	monstersInitBobs();
	effectsInitBobs();
	platformsInitBobs();
	vinesInitBobs();
	trampolinesInitBobs();
	switchesInitBobs();
	doorsInitBobs();
	bobReallocateBuffers();

	// Props tileset: onTileDraw / tileBufferRedrawAll need it; BOB setup did not.
	{
		const tLevelDef *pLevel = levelGetCurrent();
		logWrite("Props tileset: %s\n", pLevel->szPropTilesPath);
		s_pPropTiles = sunnyLoadBitmap(pLevel->szPropTilesPath, 0);
		s_pPropTilesMask = sunnyLoadBitmap(pLevel->szPropMaskPath, 0);
	}

	playerReset(levelGetSpawnTileX(), levelGetSpawnTileY());

	// Fade and free the 40 KB title card before HUD/overlay CHIP allocs.
	// With the card still up, peak was 434 KB and the largest hole was
	// 3360 bytes — overlay wants a 7680-byte interleaved bitmap.
	loadingHide();
	loadingDestroy();

	// HUD sprites on channels 0–3 and 6 (0–3 chained to the player fox
	// below the overlay), pip shots on 4–5.
	hudCreate(s_pView);
	// HUD turned sprite DMA on; keep it off until the play view is current.
	systemSetDmaBit(DMAB_SPRITE, 0);
	shotsCreate(s_pView, s_pTileBuffer);
	overlayCreate();
	loadTick();

	// Configure camera: active level width in pixels, then center on the player.
	cameraReset(
		s_pTileBuffer->pCamera, 0, 0, levelGetWidthPx(), levelGetHeightPx(), 1
	);
	cameraFollowPlayer();

	// Full initial draw, then discard any stale bob undraw state.
	// Kick still owns the blitter while the title card is up; take it
	// before the hogged tile paint. Do not poke write-only custom regs.
	if(systemBlitterIsReleasedToOs()) {
		systemGetBlitterFromOs();
	}
	gfxDump("preRedraw");
	tileBufferRedrawAll(s_pTileBuffer);
	gfxDump("postRedraw");
	bobDiscardUndraw();
	s_ubRetryRedraw = 0;
	s_ubFadeIn = 1;
	s_ubGfxDumpFrame = 1;
	playerUpdateSprite();
	loadTick();

	{
		UBYTE ubUnuse = 0;
		while(systemIsUsed() && ubUnuse < 8) {
			systemUnuse();
			++ubUnuse;
		}
		while(systemBlitterIsReleasedToOs()) {
			systemGetBlitterFromOs();
		}
		logWrite(
			"GFX unuse %u os=%u blitOs=%u\n",
			ubUnuse, systemIsUsed(), systemBlitterIsReleasedToOs()
		);
	}

	/* Card is black; install the play view black too so viewLoad is not a pop. */
	{
		UBYTE i;
		for(i = 0; i < 16; ++i) {
			s_pVPort->pPalette[i] = 0;
		}
	}
	skyDisable();
	viewLoad(s_pView);
	gfxDump("postViewLoad");
}

// Draw the already-simulated state. Called first in the loop so tile/bob
// blits run in the post-wait vblank/border, when display DMA is quiet.
// Walking used to start those blits after physics, deep into the visible
// frame, fight sprite+bitplane DMA, miss vsync, and lock at 25Hz (scroll
// looking half-speed) because vPortWaitForEnd waits for the *next* beam
// wrap when the end line is already past.
static void gameDraw(void) {
	terrainFlush();
	if(s_ubRetryRedraw) {
		gfxDump("retryRedraw");
		tileBufferRedrawAll(s_pTileBuffer);
		bobDiscardUndraw();
		s_ubRetryRedraw = 0;
	}

	tBitMap *pBack = s_pTileBuffer->pScroll->pBack;
	bobSetCurrentBuffer(pBack);
	bobBegin(pBack);
	tileBufferProcess(s_pTileBuffer);
	platformsPushBobs();
	vinesPushBobs();
	trampolinesPushBobs();
	switchesPushBobs();
	doorsPushBobs();
	pickupsPushBobs();
	monstersPushBobs();
	effectsPushBobs();
	bobPushingDone();
	bobEnd();

	if(overlayIsActive()) {
		overlayDraw(
			pBack,
			s_pTileBuffer->pCamera->uPos.uwX,
			s_pTileBuffer->pCamera->uPos.uwY,
			s_pTileBuffer->pScroll->uwBmAvailHeight
		);
	}

	scrollBufferProcess(s_pTileBuffer->pScroll);
	cameraProcess(s_pTileBuffer->pCamera);

	hudProcess();
	playerUpdateSprite();
	copProcessBlocks();

	if(s_ubFadeIn) {
		s_ubFadeIn = 0;
		gameFadeIn();
	}
}

// CPU for the *next* displayed frame. No playfield blits — safe while the
// beam is still in the current frame (the old spin-wait slack).
// Returns 1 if the caller should leave the loop (state changed).
static UBYTE gameSimulate(void) {
	platformsProcess();
	vinesProcess();
	trampolinesProcess();
	switchesProcess();
	doorsProcess();
	playerProcess();
	terrainProcess();

	if(overlayIsActive()) {
		overlayProcess();
		if(overlayIsFinished()) {
			UBYTE ubNext = (UBYTE)(levelGetIndex() + 1);
			if(ubNext >= levelGetCount()) {
				gameLeave(g_pGameWonState);
			}
			else {
				levelSetIndex(ubNext);
				gameLeave(g_pGameState);
			}
			return 1;
		}
		if(!overlayIsActive()) {
			playerUnlock();
			overlayRepair(s_pTileBuffer);
		}
	}
	else {
		// End-of-level zone: 2x3 tiles standing on the exit tile. Overlap starts
		// the result card; the actual advance waits until the card finishes.
		WORD wPX, wPY;
		UWORD uwPW, uwPH;
		playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
		WORD wPR = wPX + (WORD)uwPW - 1;
		WORD wPB = wPY + (WORD)uwPH - 1;
		WORD wZX = (WORD)(levelGetExitTileX() << 4);
		WORD wZB = (WORD)((levelGetExitTileY() + 1) << 4) - 1;
		WORD wZY = wZB - 47;
		WORD wZR = wZX + 31;
		if(wZY < 0) {
			wZY = 0;
		}
		if(
			pickupsAllGemsCollected() &&
			wZX <= wPR && wZR >= wPX && wZY <= wPB && wZB >= wPY
		) {
			playerLock();
			overlayBeginComplete(
				pickupsGetCherriesCollected(),
				pickupsGetGemsCollected(),
				monstersGetKilled()
			);
		}
		else if(playerGetHealth()) {
			signsProcess();
			pickupsProcess();
			monstersProcess();
			shotsProcess();
		}
	}

	effectsProcess();
	if(!overlayIsActive() && playerGetHealth()) {
		cameraFollowPlayer();
	}
	hudUpdate();

	if(playerConsumeDied()) {
		if(sessionOnDeath()) {
			gameRestartCurrentLevel();
			return 0;
		}
		gameLeave(g_pGameOverState);
		return 1;
	}
	return 0;
}

void gameGsLoop(void) {
	// Keys 1–9 (0 = 10th) restart into that catalog slot. stateChange runs destroy/create.
	{
		UBYTE ubCount = levelGetCount();
		for(UBYTE i = 0; i < 9; ++i) {
			if(keyUse((UBYTE)(KEY_1 + i)) && i < ubCount) {
				levelSetIndex(i);
				gameLeave(g_pGameState);
				return;
			}
		}
		if(keyUse(KEY_0) && ubCount > 9) {
			levelSetIndex(9);
			gameLeave(g_pGameState);
			return;
		}
	}

	if(keyUse(KEY_L)) {
		gfxDump("keyL");
	}
	gameDraw();
	if(s_ubGfxDumpFrame) {
		gfxDump("frame0");
		s_ubGfxDumpFrame = 0;
	}
	if(gameSimulate()) {
		return;
	}
	// If the end line is already past, do not wait for the next wrap (25Hz).
	vPortWaitUntilEnd(s_pVPort);
}

void gameGsDestroy(void) {
	systemUse();
	if(!musicConsumeKeep()) {
		musicStop();
	}
	bobManagerDestroy();
	if(s_pPristine) {
		bitmapDestroy(s_pPristine);
		s_pPristine = 0;
	}
	playerDestroy();
	pickupsDestroy();
	monstersDestroy();
	effectsDestroy();
	platformsDestroy();
	vinesDestroy();
	trampolinesDestroy();
	switchesDestroy();
	doorsDestroy();
	terrainDestroy();
	collisionDestroy();
	propsDestroy();
	shotsDestroy();
	overlayDestroy();
	hudDestroy();
	bitmapDestroy(s_pTiles);
	bitmapDestroy(s_pPropTiles);
	bitmapDestroy(s_pPropTilesMask);
	/* Tilebuffer (and scroll/camera) are VPort managers. Destroying them
	 * here and again via vPortDestroy() double-frees and crashes on level
	 * switch — leave cleanup to vPortDestroy. */
	s_pTileBuffer = 0;
	vPortDestroy(s_pVPort);
	viewDestroy(s_pView);
}
