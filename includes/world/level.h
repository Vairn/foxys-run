#ifndef LEVEL_H
#define LEVEL_H

#include <ace/types.h>
#include <ace/utils/file.h>
#include <levels/level_types.h>

/**
 * Runtime level catalog.
 *
 * Campaign order is loaded at startup (after the pak is open):
 *   1. data/levels/catalog.dat if present — one map path per line, optional
 *      graphics overrides after '|' (see catalog format below).
 *   2. Otherwise probe data/levels/level0.dat, level1.dat, … until a gap.
 *
 * Per-level graphics (optional files; missing names fall back to the shared
 * forest set):
 *   data/levels/levelN_tiles.bm              terrain tileset
 *   data/levels/levelN.plt                   16-colour playfield palette
 *   data/levels/levelN_props_tiles.bm        props tileset (level 0 uses the
 *   data/levels/levelN_props_tiles_mask.bm   shared data/tiles/props_tiles*.bm)
 *   data/levels/levelN.cop                   COLOR00 copper sky (SLCE v1)
 *
 * catalog.dat lines (comments start with #):
 *   map.dat|props.dat|prop_tiles.bm|prop_mask.bm|terrain.bm|palette.plt|music.mod|theme|title
 * Empty fields keep the convention/default for that slot.
 * theme is "winter" or "forest" (catalog fallback only). Extra art keys
 * (fort, forest_pack, woods) count as forest. The monster set lives in
 * the .dat trailer (magic 0x4D53 + 0 forest / 1 winter).
 * Extra worlds (catalog after the campaign), three maps each, sharing
 * one tileset, palette, and cast folder:
 *   Illusion Fort  level8, then level14, level15
 *   Forest pack    level12, then level16, level17
 *   Woods          level13, then level18, level19
 * Each keeps its own cast:
 *   fort    beetle slimer vulture
 *   forest  slug bee piranha
 *   woods   ant gator grasshopper woods-boss
 * Other biome tilesets stay on disk and are not packed.
 * music is a PTPlayer MOD path (sample pack stays data/audio/pf.smp).
 * title is the loading-card name (optional; numbered maps have built-in names).
 *
 * Collision uses data/levels/levelN_col.bin when present (UWORD count +
 * one byte per compact tile). Otherwise the shared compact-tileset table
 * (tilecollision.c) is used — unknown indices are empty.
 *
 * Keys 1–9 (and 0 for the 10th) restart into that catalog index in-game.
 */

#define LEVEL_MAX_COUNT 16
#define LEVEL_PATH_MAX 64
#define LEVEL_TITLE_MAX 28
#define LEVEL_HEIGHT_TILES 15
#define LEVEL_MAX_HEIGHT_TILES 60
#define LEVEL_SCREEN_HEIGHT_TILES 15
/* Longest authored level (The Islands). Tilebuffer + prop arrays size to this. */
#define LEVEL_MAX_WIDTH_TILES 360
/* Upper bounds for bob slots (max across all levels). */
#define LEVEL_MAX_ENEMIES 16
#define LEVEL_MAX_PICKUPS 32
#define LEVEL_MAX_PLATFORMS 8
#define LEVEL_MAX_VINES 4
#define LEVEL_MAX_TRAMPOLINES 8
#define LEVEL_MAX_SIGNS 8
#define LEVEL_MAX_SWITCHES 8
#define LEVEL_MAX_DOORS 8
#define PLATFORM_DEFAULT_SPEED 256 /* 8.8: 1 px/frame */
/* Terrain tileset strip: max 16px tiles (lookup table size). */
#define LEVEL_MAX_TERRAIN_TILES 512

typedef struct {
	char szMapPath[LEVEL_PATH_MAX];
	char szPropsPath[LEVEL_PATH_MAX];
	char szPropTilesPath[LEVEL_PATH_MAX];
	char szPropMaskPath[LEVEL_PATH_MAX];
	char szTilesetPath[LEVEL_PATH_MAX];
	char szPalettePath[LEVEL_PATH_MAX];
	char szColPath[LEVEL_PATH_MAX];
	char szMusicPath[LEVEL_PATH_MAX];
	char szCopPath[LEVEL_PATH_MAX];
	char szTitle[LEVEL_TITLE_MAX];
	UBYTE ubWinter;
} tLevelDef;

/** Scan catalog.dat or numbered levelN.dat files. Call after sunnyPakInit(). */
void levelCatalogLoad(void);

/** Free the catalog table. Call from genericDestroy(). */
void levelCatalogDestroy(void);

UBYTE levelGetCount(void);

void levelSetIndex(UBYTE ubIndex);

UBYTE levelGetIndex(void);

// Winter enemy / pickup skins. Prefers the .dat monster-set word.
UBYTE levelIsWinter(void);

// Read the last four bytes of the current map .dat (before sky / bobs).
void levelPeekMonsterSet(void);

/** Read the current map .dat header (width, height in tiles). */
void levelPeekMapSize(UWORD *pWidth, UWORD *pHeight);

const tLevelDef *levelGetCurrent(void);

/**
 * Playfield BOB path for the current map. Uses data/levels/levelN/<name>
 * when that file exists (per-level remap onto the map's 16-colour pal),
 * otherwise data/sprites/<name>.
 */
void levelSpritePath(char *szOut, const char *szName);

/** Loading-card name. Empty when the catalog row has no title. */
const char *levelGetTitle(void);

/**
 * Read the entity trailer from an open level .dat (file must already be past
 * the tile grid). Fills spawn, exit, enemy/pickup, optional mover, vine,
 * trampoline, sign, switch, and door tables for levelGet*().
 */
void levelLoadEntities(tFile *pFile);

/** Record map size from the .dat header (tiles). */
void levelSetWidthTiles(UWORD uwWidthTiles);
void levelSetHeightTiles(UWORD uwHeightTiles);

UWORD levelGetWidthTiles(void);
UWORD levelGetHeightTiles(void);

UWORD levelGetWidthPx(void);

UWORD levelGetHeightPx(void);

UWORD levelGetSpawnTileX(void);

UWORD levelGetSpawnTileY(void);

UWORD levelGetExitTileX(void);

UWORD levelGetExitTileY(void);

UWORD levelGetEnemyCount(void);

const tLevelEntity *levelGetEnemies(void);

UWORD levelGetPickupCount(void);

const tLevelEntity *levelGetPickups(void);

UWORD levelGetPlatformCount(void);

const tLevelPlatform *levelGetPlatforms(void);

UWORD levelGetVineCount(void);

const tLevelVine *levelGetVines(void);

UWORD levelGetTrampolineCount(void);

const tLevelTrampoline *levelGetTrampolines(void);

UWORD levelGetSignCount(void);

const tLevelEntity *levelGetSigns(void);

UWORD levelGetSwitchCount(void);

const tLevelSwitch *levelGetSwitches(void);

UWORD levelGetDoorCount(void);

const tLevelDoor *levelGetDoors(void);

#endif /* LEVEL_H */
