/* Packed copper colour0 sky + fox palette swap below the HUD. */
#include "world/sky.h"
#include "game/game_defs.h"
#include "ui/hud.h"
#include "world/level.h"
#include "assets/assets.h"

#include <ace/managers/copper.h>
#include "debug/serial_log.h"
#include <ace/managers/memory.h>
#include <ace/utils/custom.h>
#include <ace/utils/file.h>

#define SKY_GRADIENT_LINES (LEVEL_HEIGHT_TILES << TILE_SHIFT)
#define SKY_AGA_OCS_CMDS 3
#define SKY_GRADIENT_MAX_CMDS (SKY_GRADIENT_LINES * 2 + 8 + SKY_AGA_OCS_CMDS)

static tCopBlock *s_pSkyGradient;
static tCopList *s_pSkyList;

static UWORD skyGradientColor(UWORD i, UBYTE ubDither) {
	WORD wTopR = 14, wTopG = 15, wTopB = 15;
	WORD wBotR = 2,  wBotG = 4,  wBotB = 8;
	if(levelIsWinter()) {
		wTopR = 13;
		wTopG = 14;
		wTopB = 15;
		wBotR = 3;
		wBotG = 2;
		wBotB = 7;
	}
	const WORD wDenom = SKY_GRADIENT_LINES - 1;

	WORD wR = ((wTopR << 4) + ((wBotR - wTopR) * 16 * (WORD)i) / wDenom + ubDither) >> 4;
	WORD wG = ((wTopG << 4) + ((wBotG - wTopG) * 16 * (WORD)i) / wDenom + ubDither) >> 4;
	WORD wB = ((wTopB << 4) + ((wBotB - wTopB) * 16 * (WORD)i) / wDenom + ubDither) >> 4;
	if(wR < 0) { wR = 0; }
	if(wG < 0) { wG = 0; }
	if(wB < 0) { wB = 0; }
	if(wR > 15) { wR = 15; }
	if(wG > 15) { wG = 15; }
	if(wB > 15) { wB = 15; }
	return (UWORD)((wR << 8) | (wG << 4) | wB);
}

#define SKY_BLOCK_MAX 320

static tCopBlock *s_pSkyBlocks[SKY_BLOCK_MAX];
static UWORD s_uwSkyBlockCount;

static void skyBlocksReset(void) {
	s_uwSkyBlockCount = 0;
	s_pSkyGradient = 0;
}

static tCopBlock *skyAddBlock(tCopList *pList, UWORD uwY, UWORD uwCmds) {
	tCopBlock *pBlock;
	if(s_uwSkyBlockCount >= SKY_BLOCK_MAX) {
		logWrite("ERR: sky block cap\n");
		return 0;
	}
	pBlock = copBlockCreate(pList, uwCmds, 0, uwY);
	s_pSkyBlocks[s_uwSkyBlockCount++] = pBlock;
	if(!s_pSkyGradient) {
		s_pSkyGradient = pBlock;
	}
	return pBlock;
}

static void skyEmitAgaOcs(tCopList *pCopList, tCopBlock *pBlock) {
	copMove(pCopList, pBlock, &g_pCustom->fmode, 0);
	copMove(pCopList, pBlock, &g_pCustom->bplcon3, 0);
	copMove(pCopList, pBlock, &g_pCustom->bplcon4, BPLCON4_AGA_RESET);
}

/* One copper block per beam line that changes colour (and the fox palette
   line). ACE orders blocks by their header WAIT, so the scroll-buffer break
   can land between them. A single block with internal WAITs past line 255
   sorts the break after the whole sky, and the bitmap never wraps. */
static void skyEmitFoxIfNeeded(
	tCopList *pCopList, UWORD uwStartY, UWORD i, UBYTE ubChanged,
	tCopBlock *pLineBlock, UBYTE *pFoxPal
) {
	tCopBlock *pBlock;
	if(*pFoxPal || i < HUD_FOX_MIN_Y) {
		return;
	}
	pBlock = (ubChanged && pLineBlock) ? pLineBlock : skyAddBlock(
		pCopList, (UWORD)(uwStartY + i), 8
	);
	if(!pBlock) {
		return;
	}
	hudEmitFoxPalette(pCopList, pBlock);
	*pFoxPal = 1;
}

static UBYTE skyCreateFromCop(
	tView *pView, const tVPort *pVPort, const char *szPath
) {
	tFile *pFile = sunnyOpenFile(szPath);
	if(!pFile) {
		return 0;
	}

	UBYTE pMagic[4], ubVer, ubFlags;
	fileReadBytes(pFile, pMagic, 4);
	fileReadBytes(pFile, &ubVer, 1);
	fileReadBytes(pFile, &ubFlags, 1);
	UWORD uwFileLines = sunnyReadWord(pFile);
	(void)sunnyReadWord(pFile); /* waitX â€” playfield sky always waits at X=0 */
	UWORD uwMaxCmds = sunnyReadWord(pFile);
	if(
		pMagic[0] != 'S' || pMagic[1] != 'L' ||
		pMagic[2] != 'C' || pMagic[3] != 'E' ||
		ubVer != 1 || uwFileLines < 2
	) {
		logWrite("ERR: skyCreate: bad %s\n", szPath);
		fileClose(pFile);
		return 0;
	}
	(void)ubFlags;
	(void)uwMaxCmds;

	UWORD uwLines = pVPort->uwHeight;
	if(uwLines > SKY_GRADIENT_LINES) {
		uwLines = SKY_GRADIENT_LINES;
	}
	if(uwLines > uwFileLines) {
		uwLines = uwFileLines;
	}

	UBYTE *pCounts = memAllocFast(uwFileLines);
	if(!pCounts) {
		fileClose(pFile);
		return 0;
	}
	fileReadBytes(pFile, pCounts, uwFileLines);

	tCopList *pCopList = pView->pCopList;
	UWORD uwStartY = pView->ubPosY + pVPort->uwOffsY;
	s_pSkyList = pCopList;
	skyBlocksReset();
	{
		tCopBlock *pFirst = skyAddBlock(pCopList, uwStartY, 12);
		if(pFirst) {
			skyEmitAgaOcs(pCopList, pFirst);
		}
	}

	UBYTE ubFoxPal = 0;
	UWORD uwPrevColor = 0xFFFF;
	UWORD y;
	for(y = 0; y < uwFileLines; ++y) {
		UBYTE n = pCounts[y];
		UBYTE i;
		UWORD uwColor = uwPrevColor;
		UBYTE ubHaveColor0 = 0;
		for(i = 0; i < n; ++i) {
			UBYTE ubReg;
			UWORD uwWord;
			fileReadBytes(pFile, &ubReg, 1);
			uwWord = sunnyReadWord(pFile);
			/* Sprite copper owns 16â€“31; only colour 0 is the sky. */
			if(ubReg == 0) {
				uwColor = (UWORD)(uwWord & 0x0FFF);
				ubHaveColor0 = 1;
			}
		}
		if(y >= uwLines) {
			continue;
		}
		UBYTE ubChanged = 0;
		tCopBlock *pLine = 0;
		if(ubHaveColor0 && (y == 0 || uwColor != uwPrevColor)) {
			pLine = (y == 0) ? s_pSkyGradient : skyAddBlock(
				pCopList, (UWORD)(uwStartY + y), 8
			);
			if(pLine) {
				copMove(pCopList, pLine, &g_pCustom->color[0], uwColor);
			}
			uwPrevColor = uwColor;
			ubChanged = 1;
		}
		skyEmitFoxIfNeeded(
			pCopList, uwStartY, y, ubChanged, pLine, &ubFoxPal
		);
	}

	memFree(pCounts, uwFileLines);
	fileClose(pFile);
	logWrite("Sky: %s (%u lines, %u blocks)\n", szPath, uwLines, s_uwSkyBlockCount);
	return 1;
}

static void skyCreateProcedural(tView *pView, const tVPort *pVPort) {
	UWORD uwStartY = pView->ubPosY + pVPort->uwOffsY;
	UWORD uwLines = pVPort->uwHeight;
	if(uwLines > SKY_GRADIENT_LINES) {
		uwLines = SKY_GRADIENT_LINES;
	}

	tCopList *pCopList = pView->pCopList;
	s_pSkyList = pCopList;
	skyBlocksReset();
	{
		tCopBlock *pFirst = skyAddBlock(pCopList, uwStartY, 12);
		if(pFirst) {
			skyEmitAgaOcs(pCopList, pFirst);
		}
	}

	UBYTE ubFoxPal = 0;
	UWORD uwPrevColor = 0xFFFF;
	for(UWORD i = 0; i < uwLines; ++i) {
		UWORD uwColor = skyGradientColor(i, (UBYTE)((i & 1) ? 8 : 0));
		UBYTE ubChanged = (i == 0 || uwColor != uwPrevColor);
		tCopBlock *pLine = 0;
		if(ubChanged) {
			pLine = (i == 0) ? s_pSkyGradient : skyAddBlock(
				pCopList, (UWORD)(uwStartY + i), 8
			);
			if(pLine) {
				copMove(pCopList, pLine, &g_pCustom->color[0], uwColor);
			}
			uwPrevColor = uwColor;
		}
		skyEmitFoxIfNeeded(
			pCopList, uwStartY, i, ubChanged, pLine, &ubFoxPal
		);
	}
}

void skyCreate(tView *pView, const tVPort *pVPort) {
	const tLevelDef *pLevel = levelGetCurrent();
	skyBlocksReset();
	s_pSkyList = 0;
	if(pLevel && pLevel->szCopPath[0]) {
		if(skyCreateFromCop(pView, pVPort, pLevel->szCopPath)) {
			return;
		}
	}
	skyCreateProcedural(pView, pVPort);
}

void skyDisable(void) {
	UWORD i;
	if(!s_pSkyList) {
		return;
	}
	for(i = 0; i < s_uwSkyBlockCount; ++i) {
		if(s_pSkyBlocks[i]) {
			copBlockDisable(s_pSkyList, s_pSkyBlocks[i]);
		}
	}
}

void skyEnable(void) {
	UWORD i;
	if(!s_pSkyList) {
		return;
	}
	for(i = 0; i < s_uwSkyBlockCount; ++i) {
		if(s_pSkyBlocks[i]) {
			copBlockEnable(s_pSkyList, s_pSkyBlocks[i]);
		}
	}
}
