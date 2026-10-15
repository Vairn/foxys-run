/* Colour0 sky, written once. Flat maps stay one copper block.
   Tall maps keep that same command buffer and only retarget two extra
   block headers around the scroll-buffer break, so the bitmap wrap can
   land between colour waits. */
#include "world/sky.h"
#include "game/game_defs.h"
#include "ui/hud.h"
#include "world/level.h"
#include "assets/assets.h"

#include <ace/managers/copper.h>
#include <ace/managers/viewport/scrollbuffer.h>
#include "debug/serial_log.h"
#include <ace/managers/memory.h>
#include <ace/utils/custom.h>
#include <ace/utils/file.h>

#define SKY_GRADIENT_LINES (LEVEL_HEIGHT_TILES << TILE_SHIFT)
#define SKY_EVENT_MAX 288
#define SKY_CMD_MAX 512

typedef struct tSkyEv {
	UWORD uwY;
	UWORD uwColor;
	UBYTE ubColor;
	UBYTE ubFox;
	UBYTE ubAga;
} tSkyEv;

static tSkyEv s_pEv[SKY_EVENT_MAX];
static UWORD s_pMove[SKY_EVENT_MAX];
static UWORD s_uwEvCount;
static UWORD s_uwCmdCount;
static UWORD s_uwStartY;
static UWORD s_uwBreakY;
static UBYTE s_ubTall;
static UBYTE s_ubParts;
static tCopList *s_pSkyList;
static tCopBlock *s_pPart[3];
static tCopCmd *s_pOwned[3];
static UWORD s_uwOwnedMax[3];

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

static void skyAdd(UWORD uwY, UWORD uwColor, UBYTE ubColor, UBYTE ubFox, UBYTE ubAga) {
	tSkyEv *pEv;
	if(s_uwEvCount && s_pEv[s_uwEvCount - 1].uwY == uwY) {
		pEv = &s_pEv[s_uwEvCount - 1];
		if(ubColor) {
			pEv->uwColor = uwColor;
			pEv->ubColor = 1;
		}
		if(ubFox) {
			pEv->ubFox = 1;
		}
		if(ubAga) {
			pEv->ubAga = 1;
		}
		return;
	}
	if(s_uwEvCount >= SKY_EVENT_MAX) {
		return;
	}
	pEv = &s_pEv[s_uwEvCount++];
	pEv->uwY = uwY;
	pEv->uwColor = uwColor;
	pEv->ubColor = ubColor;
	pEv->ubFox = ubFox;
	pEv->ubAga = ubAga;
}

static void skyCmdWait(tCopBlock *pBlock, UBYTE ubX, UBYTE ubY) {
	if(pBlock->uwCurrCount >= pBlock->uwMaxCmds) {
		return;
	}
	copSetWait((tCopWaitCmd *)&pBlock->pCmds[pBlock->uwCurrCount], ubX, ubY);
	++pBlock->uwCurrCount;
}

static void skyBake(tCopBlock *pBlock) {
	UWORD i;
	UBYTE ubFfdf = 0;
	pBlock->uwCurrCount = 0;
	for(i = 0; i < s_uwEvCount; ++i) {
		tSkyEv *pEv = &s_pEv[i];
		if(i > 0) {
			if(!s_ubTall && pEv->uwY >= 256 && !ubFfdf) {
				skyCmdWait(pBlock, 0xDF, 0xFF);
				ubFfdf = 1;
			}
			skyCmdWait(pBlock, 0, (UBYTE)pEv->uwY);
		}
		s_pMove[i] = pBlock->uwCurrCount;
		if(pEv->ubAga) {
			copMove(s_pSkyList, pBlock, &g_pCustom->fmode, 0);
			copMove(s_pSkyList, pBlock, &g_pCustom->bplcon3, 0);
			copMove(s_pSkyList, pBlock, &g_pCustom->bplcon4, BPLCON4_AGA_RESET);
		}
		if(pEv->ubColor) {
			copMove(s_pSkyList, pBlock, &g_pCustom->color[0], pEv->uwColor);
		}
		if(pEv->ubFox) {
			hudEmitFoxPalette(s_pSkyList, pBlock);
		}
	}
	s_uwCmdCount = pBlock->uwCurrCount;
}

static UWORD skyFirst(UWORD uwY, UBYTE ubStrict) {
	UWORD i;
	for(i = 0; i < s_uwEvCount; ++i) {
		if(ubStrict) {
			if(s_pEv[i].uwY > uwY) {
				return i;
			}
		}
		else if(s_pEv[i].uwY >= uwY) {
			return i;
		}
	}
	return s_uwEvCount;
}

static void skySetPart(UBYTE ubPart, UWORD uwA, UWORD uwB) {
	tCopBlock *pBlock = s_pPart[ubPart];
	UWORD uwFrom;
	UWORD uwTo;
	if(!pBlock) {
		return;
	}
	if(uwA >= uwB || uwA >= s_uwEvCount) {
		pBlock->pCmds = s_pOwned[ubPart];
		pBlock->uwCurrCount = 0;
		copBlockWait(s_pSkyList, pBlock, 0, 0x7FFF);
		return;
	}
	uwFrom = s_pMove[uwA];
	if(uwB >= s_uwEvCount) {
		uwTo = s_uwCmdCount;
	}
	else {
		uwTo = (UWORD)(s_pMove[uwB] - 1);
	}
	if(uwTo < uwFrom) {
		uwTo = uwFrom;
	}
	pBlock->pCmds = s_pOwned[0] + uwFrom;
	pBlock->uwCurrCount = (UWORD)(uwTo - uwFrom);
	copBlockWait(s_pSkyList, pBlock, 0, s_pEv[uwA].uwY);
}

static void skyApply(UWORD uwBreak) {
	UWORD uw256 = skyFirst(256, 0);
	UWORD uwAfter;
	UWORD uwMid;
	if(uwBreak == 0xFFFF) {
		skySetPart(0, 0, uw256);
		skySetPart(1, 0, 0);
		skySetPart(2, uw256, s_uwEvCount);
		return;
	}
	if(uwBreak < 256) {
		uwAfter = skyFirst(uwBreak, 1);
		skySetPart(0, 0, uwAfter);
		skySetPart(1, uwAfter, uw256);
		skySetPart(2, uw256, s_uwEvCount);
		return;
	}
	uwMid = skyFirst(uwBreak, 1);
	skySetPart(0, 0, uw256);
	skySetPart(1, uw256, uwMid);
	skySetPart(2, uwMid, s_uwEvCount);
}

static void skyInstall(void) {
	UBYTE i;
	if(!s_pSkyList || !s_uwEvCount) {
		return;
	}
	s_ubParts = s_ubTall ? 3 : 1;
	for(i = 0; i < 3; ++i) {
		s_pPart[i] = 0;
		s_pOwned[i] = 0;
		s_uwOwnedMax[i] = 0;
	}
	for(i = 0; i < s_ubParts; ++i) {
		s_pPart[i] = copBlockCreate(s_pSkyList, SKY_CMD_MAX, 0, s_pEv[0].uwY);
		if(!s_pPart[i]) {
			return;
		}
		s_pOwned[i] = s_pPart[i]->pCmds;
		s_uwOwnedMax[i] = s_pPart[i]->uwMaxCmds;
	}
	skyBake(s_pPart[0]);
	s_uwBreakY = 0xFFFF;
	if(s_ubTall) {
		skyApply(0xFFFF);
	}
	logWrite(
		"Sky: %u events, %u cmds, %s\n",
		s_uwEvCount, s_uwCmdCount, s_ubTall ? "tall" : "flat"
	);
}

static UBYTE skyCreateFromCop(const tVPort *pVPort, const char *szPath) {
	tFile *pFile = sunnyOpenFile(szPath);
	UBYTE *pCounts;
	UWORD uwLines;
	UWORD uwFileLines;
	UWORD uwPrevColor;
	UWORD y;
	UBYTE ubFoxPal;
	UBYTE pMagic[4], ubVer, ubFlags;
	if(!pFile) {
		return 0;
	}

	fileReadBytes(pFile, pMagic, 4);
	fileReadBytes(pFile, &ubVer, 1);
	fileReadBytes(pFile, &ubFlags, 1);
	uwFileLines = sunnyReadWord(pFile);
	(void)sunnyReadWord(pFile);
	(void)sunnyReadWord(pFile);
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

	uwLines = pVPort->uwHeight;
	if(uwLines > SKY_GRADIENT_LINES) {
		uwLines = SKY_GRADIENT_LINES;
	}
	if(uwLines > uwFileLines) {
		uwLines = uwFileLines;
	}

	pCounts = memAllocFast(uwFileLines);
	if(!pCounts) {
		fileClose(pFile);
		return 0;
	}
	fileReadBytes(pFile, pCounts, uwFileLines);

	s_uwEvCount = 0;
	skyAdd(s_uwStartY, 0, 0, 0, 1);
	ubFoxPal = 0;
	uwPrevColor = 0xFFFF;
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
			if(ubReg == 0) {
				uwColor = (UWORD)(uwWord & 0x0FFF);
				ubHaveColor0 = 1;
			}
		}
		if(y >= uwLines) {
			continue;
		}
		if(ubHaveColor0 && (y == 0 || uwColor != uwPrevColor)) {
			skyAdd((UWORD)(s_uwStartY + y), uwColor, 1, 0, 0);
			uwPrevColor = uwColor;
		}
		if(!ubFoxPal && y >= HUD_FOX_MIN_Y) {
			skyAdd((UWORD)(s_uwStartY + y), 0, 0, 1, 0);
			ubFoxPal = 1;
		}
	}

	memFree(pCounts, uwFileLines);
	fileClose(pFile);
	skyInstall();
	return 1;
}

static void skyCreateProcedural(const tVPort *pVPort) {
	UWORD uwLines = pVPort->uwHeight;
	UWORD uwPrevColor = 0xFFFF;
	UWORD i;
	UBYTE ubFoxPal = 0;
	if(uwLines > SKY_GRADIENT_LINES) {
		uwLines = SKY_GRADIENT_LINES;
	}
	s_uwEvCount = 0;
	skyAdd(s_uwStartY, 0, 0, 0, 1);
	for(i = 0; i < uwLines; ++i) {
		UWORD uwColor = skyGradientColor(i, (UBYTE)((i & 1) ? 8 : 0));
		if(i == 0 || uwColor != uwPrevColor) {
			skyAdd((UWORD)(s_uwStartY + i), uwColor, 1, 0, 0);
			uwPrevColor = uwColor;
		}
		if(!ubFoxPal && i >= HUD_FOX_MIN_Y) {
			skyAdd((UWORD)(s_uwStartY + i), 0, 0, 1, 0);
			ubFoxPal = 1;
		}
	}
	skyInstall();
}

void skyCreate(tView *pView, const tVPort *pVPort) {
	const tLevelDef *pLevel = levelGetCurrent();
	s_pSkyList = pView->pCopList;
	s_uwStartY = (UWORD)(pView->ubPosY + pVPort->uwOffsY);
	s_ubTall = (UBYTE)(levelGetHeightTiles() > LEVEL_SCREEN_HEIGHT_TILES);
	s_uwEvCount = 0;
	s_ubParts = 0;
	if(pLevel && pLevel->szCopPath[0]) {
		if(skyCreateFromCop(pVPort, pLevel->szCopPath)) {
			return;
		}
	}
	skyCreateProcedural(pVPort);
}

static UWORD skyBreakY(tScrollBufferManager *pScroll) {
	const tVPort *pVPort = pScroll->sCommon.pVPort;
	UWORD uwVpHeight = pVPort->uwHeight;
	UWORD uwScrollY = SCROLLBUFFER_HEIGHT_MODULO(
		pScroll->pCamera->uPos.uwY, pScroll->uwBmAvailHeight
	);
	if(pScroll->uwBmAvailHeight - uwScrollY < uwVpHeight) {
		return (UWORD)(
			pVPort->pView->ubPosY + pVPort->uwOffsY +
			pScroll->uwBmAvailHeight - uwScrollY - 1
		);
	}
	return 0xFFFF;
}

void skySync(tScrollBufferManager *pScroll) {
	UWORD uwBreak;
	if(!s_ubTall || !s_ubParts || !pScroll || !pScroll->pCamera) {
		return;
	}
	uwBreak = skyBreakY(pScroll);
	if(uwBreak == s_uwBreakY) {
		return;
	}
	s_uwBreakY = uwBreak;
	skyApply(uwBreak);
}

void skyDetach(void) {
	UBYTE i;
	for(i = 0; i < 3; ++i) {
		if(s_pPart[i] && s_pOwned[i]) {
			s_pPart[i]->pCmds = s_pOwned[i];
			s_pPart[i]->uwMaxCmds = s_uwOwnedMax[i];
		}
		s_pPart[i] = 0;
		s_pOwned[i] = 0;
	}
	s_pSkyList = 0;
	s_ubParts = 0;
	s_ubTall = 0;
}

void skyDisable(void) {
	UBYTE i;
	if(!s_pSkyList) {
		return;
	}
	for(i = 0; i < s_ubParts; ++i) {
		if(s_pPart[i]) {
			copBlockDisable(s_pSkyList, s_pPart[i]);
		}
	}
}

void skyEnable(void) {
	UBYTE i;
	if(!s_pSkyList) {
		return;
	}
	for(i = 0; i < s_ubParts; ++i) {
		if(s_pPart[i]) {
			copBlockEnable(s_pSkyList, s_pPart[i]);
		}
	}
}
