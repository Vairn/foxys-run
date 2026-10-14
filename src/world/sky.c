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

static void skyGradientEmitWait(tCopBlock *pBlock, UWORD uwX, UWORD uwY, UBYTE *pWasLimitY) {
	if(uwY >= 0xFF) {
		if(!*pWasLimitY) {
			copSetWait(&pBlock->pCmds[pBlock->uwCurrCount++].sWait, 0xDF, 0xFF);
			*pWasLimitY = 1;
		}
		if(uwY > 0xFF) {
			copSetWait(
				&pBlock->pCmds[pBlock->uwCurrCount++].sWait,
				(UBYTE)uwX, (UBYTE)(uwY & 0xFF)
			);
		}
	}
	else {
		copSetWait(
			&pBlock->pCmds[pBlock->uwCurrCount++].sWait,
			(UBYTE)uwX, (UBYTE)uwY
		);
	}
}

/* Keep Lisa in OCS fetch every frame. viewLoad writes these once; Kick or
 * AGA leftover FMODE makes tile DMA look like static while sprites/color0
 * still look fine. Copper writes, never reads. */
static void skyEmitAgaOcs(tCopList *pCopList) {
	copMove(pCopList, s_pSkyGradient, &g_pCustom->fmode, 0);
	copMove(pCopList, s_pSkyGradient, &g_pCustom->bplcon3, 0);
	copMove(pCopList, s_pSkyGradient, &g_pCustom->bplcon4, BPLCON4_AGA_RESET);
}

static void skyEmitFoxIfNeeded(
	tCopList *pCopList, UWORD uwStartY, UWORD i, UBYTE ubChanged,
	UBYTE *pFoxPal, UBYTE *pWasLimitY
) {
	if(*pFoxPal || i < HUD_FOX_MIN_Y) {
		return;
	}
	if(!ubChanged) {
		skyGradientEmitWait(
			s_pSkyGradient, 0, uwStartY + HUD_FOX_MIN_Y, pWasLimitY
		);
	}
	hudEmitFoxPalette(pCopList, s_pSkyGradient);
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
	(void)sunnyReadWord(pFile); /* waitX — playfield sky always waits at X=0 */
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

	if(uwMaxCmds < 8) {
		uwMaxCmds = SKY_GRADIENT_MAX_CMDS;
	}
	else {
		uwMaxCmds = (UWORD)(uwMaxCmds + 8 + SKY_AGA_OCS_CMDS);
	}

	tCopList *pCopList = pView->pCopList;
	UWORD uwStartY = pView->ubPosY + pVPort->uwOffsY;
	s_pSkyList = pCopList;
	s_pSkyGradient = copBlockCreate(pCopList, uwMaxCmds, 0, uwStartY);
	skyEmitAgaOcs(pCopList);

	UBYTE ubWasLimitY = (uwStartY >= 0xFF) ? 1 : 0;
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
			/* Sprite copper owns 16–31; only colour 0 is the sky. */
			if(ubReg == 0) {
				uwColor = (UWORD)(uwWord & 0x0FFF);
				ubHaveColor0 = 1;
			}
		}
		if(y >= uwLines) {
			continue;
		}
		UBYTE ubChanged = 0;
		if(ubHaveColor0 && (y == 0 || uwColor != uwPrevColor)) {
			if(y > 0) {
				skyGradientEmitWait(
					s_pSkyGradient, 0, uwStartY + y, &ubWasLimitY
				);
			}
			copMove(pCopList, s_pSkyGradient, &g_pCustom->color[0], uwColor);
			uwPrevColor = uwColor;
			ubChanged = 1;
		}
		skyEmitFoxIfNeeded(
			pCopList, uwStartY, y, ubChanged, &ubFoxPal, &ubWasLimitY
		);
	}

	memFree(pCounts, uwFileLines);
	fileClose(pFile);
	logWrite("Sky: %s (%u lines)\n", szPath, uwLines);
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
	s_pSkyGradient = copBlockCreate(
		pCopList, SKY_GRADIENT_MAX_CMDS, 0, uwStartY
	);
	skyEmitAgaOcs(pCopList);

	UBYTE ubWasLimitY = (uwStartY >= 0xFF) ? 1 : 0;
	UBYTE ubFoxPal = 0;
	UWORD uwPrevColor = 0xFFFF;
	for(UWORD i = 0; i < uwLines; ++i) {
		UWORD uwColor = skyGradientColor(i, (UBYTE)((i & 1) ? 8 : 0));
		UBYTE ubChanged = (i == 0 || uwColor != uwPrevColor);
		if(ubChanged) {
			if(i > 0) {
				skyGradientEmitWait(
					s_pSkyGradient, 0, uwStartY + i, &ubWasLimitY
				);
			}
			copMove(
				pCopList, s_pSkyGradient, &g_pCustom->color[0], uwColor
			);
			uwPrevColor = uwColor;
		}
		skyEmitFoxIfNeeded(
			pCopList, uwStartY, i, ubChanged, &ubFoxPal, &ubWasLimitY
		);
	}
}

void skyCreate(tView *pView, const tVPort *pVPort) {
	const tLevelDef *pLevel = levelGetCurrent();
	s_pSkyGradient = 0;
	s_pSkyList = 0;
	if(pLevel && pLevel->szCopPath[0]) {
		if(skyCreateFromCop(pView, pVPort, pLevel->szCopPath)) {
			return;
		}
	}
	skyCreateProcedural(pView, pVPort);
}

void skyDisable(void) {
	if(s_pSkyGradient && s_pSkyList) {
		copBlockDisable(s_pSkyList, s_pSkyGradient);
	}
}

void skyEnable(void) {
	if(s_pSkyGradient && s_pSkyList) {
		copBlockEnable(s_pSkyList, s_pSkyGradient);
	}
}
