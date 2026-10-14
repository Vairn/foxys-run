/* SunnyLand runtime level catalog + entity load from levelN.dat. */
#include "world/level.h"
#include "game/game_defs.h"

#include "debug/serial_log.h"
#include <ace/managers/memory.h>

#include "assets/assets.h"

#define CATALOG_BUF_MAX 4096

static const char * const SZ_DEFAULT_TILESET = "data/tiles/tileset_compact.bm";
static const char * const SZ_DEFAULT_PALETTE = "data/palettes/tileset_16.plt";
static const char * const SZ_MUSIC_COOL = "data/audio/pf_cool.mod";
static const char * const SZ_MUSIC_DARK = "data/audio/pf_dark.mod";
static const char * const SZ_DEFAULT_PROP_TILES = "data/tiles/props_tiles.bm";
static const char * const SZ_DEFAULT_PROP_MASK = "data/tiles/props_tiles_mask.bm";
static const char * const SZ_CATALOG_PATH = "data/levels/catalog.dat";
static const char * const SZ_LEVEL_PREFIX = "data/levels/level";

#define LEVEL_MONSTER_SET_MAGIC 0x4D53

static tLevelDef *s_aLevels;
static ULONG s_ulLevelsBytes;
static UBYTE s_ubLevelCount;
static UBYTE s_ubLevelIndex;
static UWORD s_uwWidthTiles;
static UWORD s_uwHeightTiles = LEVEL_HEIGHT_TILES;
static UWORD s_uwSpawnTileX;
static UWORD s_uwSpawnTileY;
static UWORD s_uwExitTileX;
static UWORD s_uwExitTileY;
static UWORD s_uwEnemyCount;
static UWORD s_uwPickupCount;
static UWORD s_uwPlatformCount;
static UWORD s_uwVineCount;
static UWORD s_uwTrampolineCount;
static UWORD s_uwSignCount;
static UWORD s_uwSwitchCount;
static UWORD s_uwDoorCount;
static tLevelEntity s_aEnemies[LEVEL_MAX_ENEMIES];
static tLevelEntity s_aPickups[LEVEL_MAX_PICKUPS];
static tLevelPlatform s_aPlatforms[LEVEL_MAX_PLATFORMS];
static tLevelVine s_aVines[LEVEL_MAX_VINES];
static tLevelTrampoline s_aTrampolines[LEVEL_MAX_TRAMPOLINES];
static tLevelEntity s_aSigns[LEVEL_MAX_SIGNS];
static tLevelSwitch s_aSwitches[LEVEL_MAX_SWITCHES];
static tLevelDoor s_aDoors[LEVEL_MAX_DOORS];
static UBYTE s_ubMonsterSet;
static UBYTE s_ubMonsterSetFromDat;

static const char * const SZ_DEFAULT_TITLE[] = {
	"THE GROVE",
	"THE MINE",
	"THE TERRACES",
	"THE ISLANDS",
	"THE PINES",
	"THE FAR PINES",
	"THE TRAPS",
	"ILLUSION WOOD",
	"ILLUSION FORT",
	"JUNGLE RUINS",
	"MAGICAL ROAD",
	"TALL FOREST",
	"THE THICKET",
	"THE WOODS"
};

static void copyBounded(char *szDst, const char *szSrc, UWORD uwMax) {
	UWORD i = 0;
	if(!szSrc) {
		szDst[0] = '\0';
		return;
	}
	while(szSrc[i] && i < uwMax - 1) {
		szDst[i] = szSrc[i];
		++i;
	}
	szDst[i] = '\0';
}

static void copyPath(char *szDst, const char *szSrc) {
	copyBounded(szDst, szSrc, LEVEL_PATH_MAX);
}

static void copyTitle(char *szDst, const char *szSrc) {
	copyBounded(szDst, szSrc, LEVEL_TITLE_MAX);
}

static void setDefaultTitle(tLevelDef *pDef, UBYTE ubIndex) {
	if(ubIndex < sizeof(SZ_DEFAULT_TITLE) / sizeof(SZ_DEFAULT_TITLE[0])) {
		copyTitle(pDef->szTitle, SZ_DEFAULT_TITLE[ubIndex]);
	}
	else {
		pDef->szTitle[0] = '\0';
	}
}

static void joinPath(char *szDst, const char *szStem, const char *szSuffix) {
	UWORD i = 0;
	while(szStem[i] && i < LEVEL_PATH_MAX - 1) {
		szDst[i] = szStem[i];
		++i;
	}
	UWORD j = 0;
	while(szSuffix[j] && i < LEVEL_PATH_MAX - 1) {
		szDst[i++] = szSuffix[j++];
	}
	szDst[i] = '\0';
}

static void mapStem(const char *szMap, char *szStem) {
	copyPath(szStem, szMap);
	UWORD uwLen = 0;
	UWORD uwDot = 0;
	while(szStem[uwLen]) {
		if(szStem[uwLen] == '.') {
			uwDot = uwLen;
		}
		++uwLen;
	}
	if(uwDot) {
		szStem[uwDot] = '\0';
	}
}

static UBYTE tryLevelSprite(const char *szStem, const char *szName, char *szOut) {
	char szSuf[LEVEL_PATH_MAX];
	char szTry[LEVEL_PATH_MAX];
	if(!szStem || !szStem[0] || !szName || !szName[0]) {
		return 0;
	}
	szSuf[0] = '/';
	copyBounded(&szSuf[1], szName, LEVEL_PATH_MAX - 1);
	joinPath(szTry, szStem, szSuf);
	if(!sunnyFileExists(szTry)) {
		return 0;
	}
	copyPath(szOut, szTry);
	return 1;
}

/* data/levels/level8_tiles.bm -> data/levels/level8, where that cast lives. */
static void tilesetSpriteStem(const char *szTileset, char *szStem) {
	const char *szMark = "_tiles.bm";
	UWORD n = 0;
	UWORD mark = 0;
	szStem[0] = '\0';
	if(!szTileset || !szTileset[0]) {
		return;
	}
	while(szTileset[n]) {
		++n;
	}
	while(szMark[mark]) {
		++mark;
	}
	if(n < mark) {
		return;
	}
	for(UWORD i = 0; i < mark; ++i) {
		if(szTileset[n - mark + i] != szMark[i]) {
			return;
		}
	}
	if(n - mark >= LEVEL_PATH_MAX) {
		return;
	}
	for(UWORD i = 0; i < n - mark; ++i) {
		szStem[i] = szTileset[i];
	}
	szStem[n - mark] = '\0';
}

void levelSpritePath(char *szOut, const char *szName) {
	const tLevelDef *pDef = levelGetCurrent();
	char szStem[LEVEL_PATH_MAX];
	if(pDef && szName && szName[0]) {
		if(pDef->szMapPath[0]) {
			mapStem(pDef->szMapPath, szStem);
			if(tryLevelSprite(szStem, szName, szOut)) {
				return;
			}
		}
		/* Sequels share the parent world's cast via the tileset path. */
		tilesetSpriteStem(pDef->szTilesetPath, szStem);
		if(tryLevelSprite(szStem, szName, szOut)) {
			return;
		}
	}
	joinPath(szOut, "data/sprites/", szName);
}

static void pickOptional(
	char *szDst, const char *szStem, const char *szSuffix, const char *szDefault
) {
	char szTry[LEVEL_PATH_MAX];
	joinPath(szTry, szStem, szSuffix);
	if(sunnyFileExists(szTry)) {
		copyPath(szDst, szTry);
	}
	else {
		copyPath(szDst, szDefault);
	}
}

static UBYTE pathsEqual(const char *szA, const char *szB) {
	UWORD i = 0;
	while(szA[i] && szA[i] == szB[i]) {
		++i;
	}
	return (UBYTE)(szA[i] == szB[i]);
}

static void setDefaultMusic(tLevelDef *pDef) {
	copyPath(pDef->szMusicPath, pDef->ubWinter ? SZ_MUSIC_DARK : SZ_MUSIC_COOL);
}

static void applyThemeFromGraphics(tLevelDef *pDef) {
	/* A forest map can ship its own tileset (level0_tiles.bm) and still
	 * use the forest palette. Winter is the custom .plt. Catalog field 8
	 * overrides this guess. */
	pDef->ubWinter = (UBYTE)!pathsEqual(pDef->szPalettePath, SZ_DEFAULT_PALETTE);
	setDefaultMusic(pDef);
}

static void fillLevelFromMap(tLevelDef *pDef, const char *szMap) {
	char szStem[LEVEL_PATH_MAX];
	copyPath(pDef->szMapPath, szMap);
	mapStem(szMap, szStem);
	joinPath(pDef->szPropsPath, szStem, "_props.dat");
	pickOptional(
		pDef->szPropTilesPath, szStem, "_props_tiles.bm", SZ_DEFAULT_PROP_TILES
	);
	pickOptional(
		pDef->szPropMaskPath, szStem, "_props_tiles_mask.bm", SZ_DEFAULT_PROP_MASK
	);
	pickOptional(pDef->szTilesetPath, szStem, "_tiles.bm", SZ_DEFAULT_TILESET);
	pickOptional(pDef->szPalettePath, szStem, ".plt", SZ_DEFAULT_PALETTE);
	pickOptional(pDef->szColPath, szStem, "_col.bin", "");
	pickOptional(pDef->szCopPath, szStem, ".cop", "");
	applyThemeFromGraphics(pDef);
	setDefaultTitle(pDef, s_ubLevelCount);
}

static void makeNumberedDat(char *szOut, UBYTE ubIndex) {
	joinPath(szOut, SZ_LEVEL_PREFIX, "");
	UWORD i = 0;
	while(szOut[i]) {
		++i;
	}
	if(ubIndex >= 10) {
		if(i < LEVEL_PATH_MAX - 1) {
			szOut[i++] = (char)('0' + ubIndex / 10);
		}
		ubIndex = (UBYTE)(ubIndex % 10);
	}
	if(i < LEVEL_PATH_MAX - 1) {
		szOut[i++] = (char)('0' + ubIndex);
	}
	const char *szExt = ".dat";
	UWORD j = 0;
	while(szExt[j] && i < LEVEL_PATH_MAX - 1) {
		szOut[i++] = szExt[j++];
	}
	szOut[i] = '\0';
}

static void overrideIfSet(char *szDst, const char *szField) {
	if(szField && szField[0]) {
		copyPath(szDst, szField);
	}
}

static char *skipSpaces(char *sz) {
	while(*sz == ' ' || *sz == '\t') {
		++sz;
	}
	return sz;
}

static void trimTrailing(char *sz) {
	UWORD n = 0;
	while(sz[n]) {
		++n;
	}
	while(n && (sz[n - 1] == ' ' || sz[n - 1] == '\t' || sz[n - 1] == '\r')) {
		sz[--n] = '\0';
	}
}

static UBYTE parseCatalogLine(char *szLine) {
	char *pFields[9];
	UBYTE ubFields = 0;
	char *p = skipSpaces(szLine);
	if(*p == '\0' || *p == '#') {
		return 0;
	}
	pFields[ubFields++] = p;
	while(*p && ubFields < 9) {
		if(*p == '|') {
			*p = '\0';
			pFields[ubFields++] = p + 1;
		}
		++p;
	}
	UWORD i;
	for(i = 0; i < ubFields; ++i) {
		pFields[i] = skipSpaces(pFields[i]);
		trimTrailing(pFields[i]);
	}
	if(!pFields[0][0]) {
		return 0;
	}
	if(!s_aLevels || s_ubLevelCount >= LEVEL_MAX_COUNT) {
		logWrite("WARN: catalog exceeds %u levels — ignoring extra\n", LEVEL_MAX_COUNT);
		return 0;
	}
	tLevelDef *pDef = &s_aLevels[s_ubLevelCount];
	fillLevelFromMap(pDef, pFields[0]);
	if(ubFields > 1) {
		overrideIfSet(pDef->szPropsPath, pFields[1]);
	}
	if(ubFields > 2) {
		overrideIfSet(pDef->szPropTilesPath, pFields[2]);
	}
	if(ubFields > 3) {
		overrideIfSet(pDef->szPropMaskPath, pFields[3]);
	}
	if(ubFields > 4) {
		overrideIfSet(pDef->szTilesetPath, pFields[4]);
	}
	if(ubFields > 5) {
		overrideIfSet(pDef->szPalettePath, pFields[5]);
	}
	applyThemeFromGraphics(pDef);
	if(ubFields > 7 && pFields[7][0]) {
		/* Only "winter" / "w". Art keys like woods / forest_pack stay forest. */
		const char *szTheme = pFields[7];
		UBYTE ubWinter = 0;
		if((szTheme[0] == 'w' || szTheme[0] == 'W') && szTheme[1] == '\0') {
			ubWinter = 1;
		}
		else {
			const char *szWant = "winter";
			UWORD n = 0;
			while(szWant[n] && szTheme[n]) {
				char c = szTheme[n];
				if(c >= 'A' && c <= 'Z') {
					c = (char)(c - 'A' + 'a');
				}
				if(c != szWant[n]) {
					break;
				}
				++n;
			}
			ubWinter = (UBYTE)(szWant[n] == '\0' && szTheme[n] == '\0');
		}
		pDef->ubWinter = ubWinter;
		setDefaultMusic(pDef);
	}
	if(ubFields > 6) {
		overrideIfSet(pDef->szMusicPath, pFields[6]);
	}
	if(ubFields > 8 && pFields[8][0]) {
		copyTitle(pDef->szTitle, pFields[8]);
	}
	++s_ubLevelCount;
	return 1;
}

static void parseCatalogBuf(char *szBuf) {
	char *pLine = szBuf;
	while(*pLine) {
		char *pEnd = pLine;
		while(*pEnd && *pEnd != '\n') {
			++pEnd;
		}
		char cSaved = *pEnd;
		*pEnd = '\0';
		parseCatalogLine(pLine);
		if(!cSaved) {
			break;
		}
		pLine = pEnd + 1;
	}
}

static void probeNumberedLevels(void) {
	UBYTE i;
	if(!s_aLevels) {
		return;
	}
	for(i = 0; i < LEVEL_MAX_COUNT; ++i) {
		char szMap[LEVEL_PATH_MAX];
		makeNumberedDat(szMap, i);
		if(!sunnyFileExists(szMap)) {
			break;
		}
		fillLevelFromMap(&s_aLevels[s_ubLevelCount], szMap);
		++s_ubLevelCount;
	}
}

static void logCatalog(void) {
	UBYTE i;
	logWrite("Level catalog: %u levels\n", s_ubLevelCount);
	for(i = 0; i < s_ubLevelCount; ++i) {
		logWrite(
			"  %u: %s  \"%s\"  tiles=%s  pal=%s  cop=%s  music=%s  %s\n",
			i, s_aLevels[i].szMapPath,
			s_aLevels[i].szTitle[0] ? s_aLevels[i].szTitle : "-",
			s_aLevels[i].szTilesetPath,
			s_aLevels[i].szPalettePath,
			s_aLevels[i].szCopPath[0] ? s_aLevels[i].szCopPath : "-",
			s_aLevels[i].szMusicPath,
			s_aLevels[i].ubWinter ? "winter" : "forest"
		);
	}
}

void levelCatalogDestroy(void) {
	if(s_aLevels) {
		memFree(s_aLevels, s_ulLevelsBytes);
		s_aLevels = 0;
	}
	s_ulLevelsBytes = 0;
	s_ubLevelCount = 0;
	s_ubLevelIndex = 0;
}

void levelCatalogLoad(void) {
	s_ubLevelCount = 0;
	s_ubLevelIndex = 0;
	if(!s_aLevels) {
		s_ulLevelsBytes = sizeof(tLevelDef) * LEVEL_MAX_COUNT;
		s_aLevels = memAllocFastClear(s_ulLevelsBytes);
		if(!s_aLevels) {
			logWrite("ERR: catalog alloc %lu bytes\n", s_ulLevelsBytes);
			s_ulLevelsBytes = 0;
			return;
		}
	}
	if(sunnyFileExists(SZ_CATALOG_PATH)) {
		tFile *pFile = sunnyOpenFile(SZ_CATALOG_PATH);
		if(pFile) {
			LONG lSize = fileGetSize(pFile);
			ULONG ulBuf;
			char *szBuf;
			if(lSize < 0) {
				lSize = 0;
			}
			if(lSize > CATALOG_BUF_MAX - 1) {
				lSize = CATALOG_BUF_MAX - 1;
			}
			ulBuf = (ULONG)lSize + 1;
			szBuf = memAllocFast(ulBuf);
			if(szBuf) {
				ULONG ulRead = fileReadBytes(pFile, (UBYTE *)szBuf, (ULONG)lSize);
				szBuf[ulRead] = '\0';
				parseCatalogBuf(szBuf);
				memFree(szBuf, ulBuf);
			}
			else {
				logWrite("ERR: catalog buffer alloc %lu bytes\n", ulBuf);
			}
			fileClose(pFile);
		}
	}
	if(!s_ubLevelCount) {
		probeNumberedLevels();
	}
	if(!s_ubLevelCount) {
		logWrite("ERR: no levels found (catalog.dat or data/levels/level0.dat)\n");
	}
	logCatalog();
}

UBYTE levelGetCount(void) {
	return s_ubLevelCount;
}

void levelSetIndex(UBYTE ubIndex) {
	if(!s_ubLevelCount) {
		s_ubLevelIndex = 0;
		s_ubMonsterSetFromDat = 0;
		return;
	}
	if(ubIndex >= s_ubLevelCount) {
		ubIndex = 0;
	}
	s_ubLevelIndex = ubIndex;
	s_ubMonsterSetFromDat = 0;
}

UBYTE levelGetIndex(void) {
	return s_ubLevelIndex;
}

static void levelApplyMonsterSet(UWORD uwSet) {
	s_ubMonsterSet = uwSet ? 1 : 0;
	s_ubMonsterSetFromDat = 1;
}

void levelPeekMonsterSet(void) {
	const tLevelDef *pLevel = levelGetCurrent();
	tFile *pFile;
	LONG lSize;

	s_ubMonsterSetFromDat = 0;
	if(!pLevel || !pLevel->szMapPath[0]) {
		return;
	}
	pFile = sunnyOpenFile(pLevel->szMapPath);
	if(!pFile) {
		return;
	}
	lSize = fileGetSize(pFile);
	if(lSize >= 4) {
		UWORD uwMagic, uwSet;
		fileSeek(pFile, lSize - 4, FILE_SEEK_SET);
		uwMagic = sunnyReadWord(pFile);
		uwSet = sunnyReadWord(pFile);
		if(uwMagic == LEVEL_MONSTER_SET_MAGIC && uwSet <= 1) {
			levelApplyMonsterSet(uwSet);
		}
	}
	fileClose(pFile);
}

void levelPeekMapSize(UWORD *pWidth, UWORD *pHeight) {
	const tLevelDef *pLevel = levelGetCurrent();
	tFile *pFile;
	UWORD uwW = 20;
	UWORD uwH = LEVEL_HEIGHT_TILES;
	if(pLevel && pLevel->szMapPath[0]) {
		pFile = sunnyOpenFile(pLevel->szMapPath);
		if(pFile) {
			uwW = sunnyReadWord(pFile);
			uwH = sunnyReadWord(pFile);
			fileClose(pFile);
		}
	}
	if(!uwW) {
		uwW = 20;
	}
	if(uwW > LEVEL_MAX_WIDTH_TILES) {
		uwW = LEVEL_MAX_WIDTH_TILES;
	}
	if(uwH < LEVEL_SCREEN_HEIGHT_TILES) {
		uwH = LEVEL_SCREEN_HEIGHT_TILES;
	}
	if(uwH > LEVEL_MAX_HEIGHT_TILES) {
		uwH = LEVEL_MAX_HEIGHT_TILES;
	}
	if(pWidth) {
		*pWidth = uwW;
	}
	if(pHeight) {
		*pHeight = uwH;
	}
	s_uwWidthTiles = uwW;
	s_uwHeightTiles = uwH;
}

UBYTE levelIsWinter(void) {
	if(s_ubMonsterSetFromDat) {
		return s_ubMonsterSet;
	}
	const tLevelDef *pLevel = levelGetCurrent();
	return (pLevel && pLevel->ubWinter) ? 1 : 0;
}

const tLevelDef *levelGetCurrent(void) {
	if(!s_aLevels) {
		return 0;
	}
	if(!s_ubLevelCount) {
		return &s_aLevels[0];
	}
	return &s_aLevels[s_ubLevelIndex];
}

const char *levelGetTitle(void) {
	const tLevelDef *pLevel = levelGetCurrent();
	return (pLevel && pLevel->szTitle[0]) ? pLevel->szTitle : "";
}

void levelSetWidthTiles(UWORD uwWidthTiles) {
	if(uwWidthTiles > LEVEL_MAX_WIDTH_TILES) {
		uwWidthTiles = LEVEL_MAX_WIDTH_TILES;
	}
	s_uwWidthTiles = uwWidthTiles;
}

void levelSetHeightTiles(UWORD uwHeightTiles) {
	if(uwHeightTiles < LEVEL_SCREEN_HEIGHT_TILES) {
		uwHeightTiles = LEVEL_SCREEN_HEIGHT_TILES;
	}
	if(uwHeightTiles > LEVEL_MAX_HEIGHT_TILES) {
		uwHeightTiles = LEVEL_MAX_HEIGHT_TILES;
	}
	s_uwHeightTiles = uwHeightTiles;
}

UWORD levelGetHeightTiles(void) {
	return s_uwHeightTiles;
}

static void readEntityList(
	tFile *pFile, tLevelEntity *pOut, UWORD *pCount, UWORD uwMax, const char *szLabel
) {
	UWORD uwCount = sunnyReadWord(pFile);
	if(uwCount > uwMax) {
		logWrite(
			"WARN: %s count %u exceeds max %u — truncating\n",
			szLabel, uwCount, uwMax
		);
	}
	UWORD uwKeep = (uwCount > uwMax) ? uwMax : uwCount;
	for(UWORD i = 0; i < uwCount; ++i) {
		UWORD uwTileX = sunnyReadWord(pFile);
		UWORD uwTileY = sunnyReadWord(pFile);
		UWORD uwType = sunnyReadWord(pFile);
		if(i < uwKeep) {
			pOut[i].uwTileX = uwTileX;
			pOut[i].uwTileY = uwTileY;
			pOut[i].ubType = (UBYTE)uwType;
		}
	}
	*pCount = uwKeep;
}

void levelLoadEntities(tFile *pFile) {
	s_uwSpawnTileX = sunnyReadWord(pFile);
	s_uwSpawnTileY = sunnyReadWord(pFile);
	readEntityList(
		pFile, s_aEnemies, &s_uwEnemyCount, LEVEL_MAX_ENEMIES, "enemy"
	);
	readEntityList(
		pFile, s_aPickups, &s_uwPickupCount, LEVEL_MAX_PICKUPS, "pickup"
	);
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 4) {
		s_uwExitTileX = sunnyReadWord(pFile);
		s_uwExitTileY = sunnyReadWord(pFile);
	}
	else {
		s_uwExitTileX = (s_uwWidthTiles > 3) ? (UWORD)(s_uwWidthTiles - 3) : 0;
		s_uwExitTileY = s_uwSpawnTileY;
	}
	if(s_uwExitTileX >= s_uwWidthTiles && s_uwWidthTiles > 0) {
		s_uwExitTileX = (UWORD)(s_uwWidthTiles - 1);
	}
	if(s_uwExitTileY >= s_uwHeightTiles && s_uwHeightTiles > 0) {
		s_uwExitTileY = (UWORD)(s_uwHeightTiles - 1);
	}

	s_uwPlatformCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		UWORD uwCount = sunnyReadWord(pFile);
		if(uwCount > LEVEL_MAX_PLATFORMS) {
			logWrite(
				"WARN: platform count %u exceeds max %u — truncating\n",
				uwCount, LEVEL_MAX_PLATFORMS
			);
		}
		UWORD uwKeep = (uwCount > LEVEL_MAX_PLATFORMS) ? LEVEL_MAX_PLATFORMS : uwCount;
		for(UWORD i = 0; i < uwCount; ++i) {
			UWORD uwStartX = sunnyReadWord(pFile);
			UWORD uwStartY = sunnyReadWord(pFile);
			UWORD uwEndX = sunnyReadWord(pFile);
			UWORD uwEndY = sunnyReadWord(pFile);
			UWORD uwSpeed = sunnyReadWord(pFile);
			if(i >= uwKeep) {
				continue;
			}
			WORD wDx = (WORD)uwEndX - (WORD)uwStartX;
			WORD wDy = (WORD)uwEndY - (WORD)uwStartY;
			if(wDx < 0) {
				wDx = (WORD)-wDx;
			}
			if(wDy < 0) {
				wDy = (WORD)-wDy;
			}
			if(wDx >= wDy) {
				uwEndY = uwStartY;
			}
			else {
				uwEndX = uwStartX;
			}
			if(!uwSpeed) {
				uwSpeed = PLATFORM_DEFAULT_SPEED;
			}
			s_aPlatforms[i].uwStartX = uwStartX;
			s_aPlatforms[i].uwStartY = uwStartY;
			s_aPlatforms[i].uwEndX = uwEndX;
			s_aPlatforms[i].uwEndY = uwEndY;
			s_aPlatforms[i].uwSpeed = uwSpeed;
			s_aPlatforms[i].uwChannel = 0;
		}
		s_uwPlatformCount = uwKeep;
	}

	s_uwVineCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		UWORD uwCount = sunnyReadWord(pFile);
		if(uwCount > LEVEL_MAX_VINES) {
			logWrite(
				"WARN: vine count %u exceeds max %u — truncating\n",
				uwCount, LEVEL_MAX_VINES
			);
		}
		UWORD uwKeep = (uwCount > LEVEL_MAX_VINES) ? LEVEL_MAX_VINES : uwCount;
		for(UWORD i = 0; i < uwCount; ++i) {
			UWORD uwAnchorX = sunnyReadWord(pFile);
			UWORD uwAnchorY = sunnyReadWord(pFile);
			UWORD uwLength = sunnyReadWord(pFile);
			if(i >= uwKeep) {
				continue;
			}
			if(uwLength < VINE_LENGTH_MIN) {
				uwLength = VINE_LENGTH_DEFAULT;
			}
			if(uwLength > VINE_LENGTH_MAX) {
				uwLength = VINE_LENGTH_MAX;
			}
			s_aVines[i].uwAnchorX = uwAnchorX;
			s_aVines[i].uwAnchorY = uwAnchorY;
			s_aVines[i].uwLength = uwLength;
		}
		s_uwVineCount = uwKeep;
	}

	s_uwTrampolineCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		UWORD uwCount = sunnyReadWord(pFile);
		if(uwCount > LEVEL_MAX_TRAMPOLINES) {
			logWrite(
				"WARN: trampoline count %u exceeds max %u — truncating\n",
				uwCount, LEVEL_MAX_TRAMPOLINES
			);
		}
		UWORD uwKeep = (
			uwCount > LEVEL_MAX_TRAMPOLINES
		) ? LEVEL_MAX_TRAMPOLINES : uwCount;
		for(UWORD i = 0; i < uwCount; ++i) {
			UWORD uwTileX = sunnyReadWord(pFile);
			UWORD uwTileY = sunnyReadWord(pFile);
			if(i >= uwKeep) {
				continue;
			}
			s_aTrampolines[i].uwTileX = uwTileX;
			s_aTrampolines[i].uwTileY = uwTileY;
		}
		s_uwTrampolineCount = uwKeep;
	}

	s_uwSignCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		readEntityList(
			pFile, s_aSigns, &s_uwSignCount, LEVEL_MAX_SIGNS, "sign"
		);
	}

	s_uwSwitchCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		UWORD uwCount = sunnyReadWord(pFile);
		if(uwCount > LEVEL_MAX_SWITCHES) {
			logWrite(
				"WARN: switch count %u exceeds max %u — truncating\n",
				uwCount, LEVEL_MAX_SWITCHES
			);
		}
		UWORD uwKeep = (uwCount > LEVEL_MAX_SWITCHES) ? LEVEL_MAX_SWITCHES : uwCount;
		for(UWORD i = 0; i < uwCount; ++i) {
			UWORD uwTileX = sunnyReadWord(pFile);
			UWORD uwTileY = sunnyReadWord(pFile);
			UWORD uwChannel = sunnyReadWord(pFile);
			UWORD uwFlags = sunnyReadWord(pFile);
			if(i >= uwKeep) {
				continue;
			}
			if(uwChannel > SWITCH_CHANNEL_MAX) {
				uwChannel = SWITCH_CHANNEL_MAX;
			}
			s_aSwitches[i].uwTileX = uwTileX;
			s_aSwitches[i].uwTileY = uwTileY;
			s_aSwitches[i].uwChannel = uwChannel;
			s_aSwitches[i].uwFlags = uwFlags;
		}
		s_uwSwitchCount = uwKeep;
	}

	s_uwDoorCount = 0;
	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 2) {
		UWORD uwCount = sunnyReadWord(pFile);
		if(uwCount > LEVEL_MAX_DOORS) {
			logWrite(
				"WARN: door count %u exceeds max %u — truncating\n",
				uwCount, LEVEL_MAX_DOORS
			);
		}
		UWORD uwKeep = (uwCount > LEVEL_MAX_DOORS) ? LEVEL_MAX_DOORS : uwCount;
		for(UWORD i = 0; i < uwCount; ++i) {
			UWORD uwTileX = sunnyReadWord(pFile);
			UWORD uwTileY = sunnyReadWord(pFile);
			UWORD uwChannel = sunnyReadWord(pFile);
			UWORD uwKind = sunnyReadWord(pFile);
			UWORD uwEndX = sunnyReadWord(pFile);
			UWORD uwEndY = sunnyReadWord(pFile);
			if(i >= uwKeep) {
				continue;
			}
			if(uwChannel > SWITCH_CHANNEL_MAX) {
				uwChannel = SWITCH_CHANNEL_MAX;
			}
			if(uwKind > DOOR_KIND_TERRAIN) {
				uwKind = DOOR_KIND_VANISH;
			}
			s_aDoors[i].uwTileX = uwTileX;
			s_aDoors[i].uwTileY = uwTileY;
			s_aDoors[i].uwChannel = uwChannel;
			s_aDoors[i].uwKind = uwKind;
			s_aDoors[i].uwEndX = uwEndX;
			s_aDoors[i].uwEndY = uwEndY;
		}
		s_uwDoorCount = uwKeep;
	}

	if(
		s_uwPlatformCount &&
		(fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= (LONG)(s_uwPlatformCount * 2)
	) {
		for(UWORD i = 0; i < s_uwPlatformCount; ++i) {
			UWORD uwChannel = sunnyReadWord(pFile);
			if(uwChannel > SWITCH_CHANNEL_MAX) {
				uwChannel = SWITCH_CHANNEL_MAX;
			}
			s_aPlatforms[i].uwChannel = uwChannel;
		}
	}

	if((fileGetSize(pFile) - (LONG)fileGetPos(pFile)) >= 4) {
		UWORD uwMagic = sunnyReadWord(pFile);
		UWORD uwSet = sunnyReadWord(pFile);
		if(uwMagic == LEVEL_MONSTER_SET_MAGIC && uwSet <= 1) {
			levelApplyMonsterSet(uwSet);
		}
	}

	logWrite(
		"Entities: spawn %u,%u exit %u,%u — %u enemies, %u pickups, %u movers, "
		"%u vines, %u trampolines, %u signs, %u switches, %u doors\n",
		s_uwSpawnTileX, s_uwSpawnTileY, s_uwExitTileX, s_uwExitTileY,
		s_uwEnemyCount, s_uwPickupCount, s_uwPlatformCount,
		s_uwVineCount, s_uwTrampolineCount, s_uwSignCount,
		s_uwSwitchCount, s_uwDoorCount
	);
}

UWORD levelGetWidthTiles(void) {
	return s_uwWidthTiles;
}

UWORD levelGetWidthPx(void) {
	return (UWORD)(s_uwWidthTiles << TILE_SHIFT);
}

UWORD levelGetHeightPx(void) {
	return (UWORD)(s_uwHeightTiles << TILE_SHIFT);
}

UWORD levelGetSpawnTileX(void) {
	return s_uwSpawnTileX;
}

UWORD levelGetSpawnTileY(void) {
	return s_uwSpawnTileY;
}

UWORD levelGetExitTileX(void) {
	return s_uwExitTileX;
}

UWORD levelGetExitTileY(void) {
	return s_uwExitTileY;
}

UWORD levelGetEnemyCount(void) {
	return s_uwEnemyCount;
}

const tLevelEntity *levelGetEnemies(void) {
	return s_aEnemies;
}

UWORD levelGetPickupCount(void) {
	return s_uwPickupCount;
}

const tLevelEntity *levelGetPickups(void) {
	return s_aPickups;
}

UWORD levelGetPlatformCount(void) {
	return s_uwPlatformCount;
}

const tLevelPlatform *levelGetPlatforms(void) {
	return s_aPlatforms;
}

UWORD levelGetVineCount(void) {
	return s_uwVineCount;
}

const tLevelVine *levelGetVines(void) {
	return s_aVines;
}

UWORD levelGetTrampolineCount(void) {
	return s_uwTrampolineCount;
}

const tLevelTrampoline *levelGetTrampolines(void) {
	return s_aTrampolines;
}

UWORD levelGetSignCount(void) {
	return s_uwSignCount;
}

const tLevelEntity *levelGetSigns(void) {
	return s_aSigns;
}

UWORD levelGetSwitchCount(void) {
	return s_uwSwitchCount;
}

const tLevelSwitch *levelGetSwitches(void) {
	return s_aSwitches;
}

UWORD levelGetDoorCount(void) {
	return s_uwDoorCount;
}

const tLevelDoor *levelGetDoors(void) {
	return s_aDoors;
}
