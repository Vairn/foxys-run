#include "intro/slicecop.h"
#include "assets/assets.h"

#include "debug/serial_log.h"
#include <ace/managers/memory.h>
#include <ace/utils/custom.h>
#include <ace/utils/file.h>

static void sliceCopEmitWait(
	tCopBlock *pBlock, UWORD uwX, UWORD uwY, UBYTE *pWasLimitY
) {
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

tCopBlock *sliceCopCreate(
	tView *pView, const tVPort *pVPort, const char *szPath
) {
	tFile *pFile;
	UBYTE pMagic[4], ubVer, ubFlags, *pCounts;
	UWORD uwLines, uwWaitX, uwMaxCmds, uwStartY, y;
	tCopList *pCopList;
	tCopBlock *pBlock;
	UBYTE ubWasLimitY;

	pFile = sunnyOpenFile(szPath);
	if(!pFile) {
		return 0;
	}

	fileReadBytes(pFile, pMagic, 4);
	fileReadBytes(pFile, &ubVer, 1);
	fileReadBytes(pFile, &ubFlags, 1);
	uwLines = sunnyReadWord(pFile);
	uwWaitX = sunnyReadWord(pFile);
	uwMaxCmds = sunnyReadWord(pFile);
	if(
		pMagic[0] != 'S' || pMagic[1] != 'L' ||
		pMagic[2] != 'C' || pMagic[3] != 'E' ||
		ubVer != 1 || uwLines < 2
	) {
		logWrite("ERR: sliceCopCreate: bad %s\n", szPath);
		fileClose(pFile);
		return 0;
	}
	(void)ubFlags;

	pCounts = memAllocFast(uwLines);
	fileReadBytes(pFile, pCounts, uwLines);
	/* Line 0 is already in the viewport palette. */
	fileSeek(pFile, (LONG)pCounts[0] * 3, FILE_SEEK_CURRENT);

	pCopList = pView->pCopList;
	uwStartY = pView->ubPosY + pVPort->uwOffsY;
	pBlock = copBlockCreate(pCopList, uwMaxCmds, uwWaitX, uwStartY);
	ubWasLimitY = (uwStartY >= 0xFF) ? 1 : 0;

	for(y = 1; y < uwLines; ++y) {
		UBYTE n = pCounts[y];
		UBYTE i;
		if(!n) {
			continue;
		}
		if(pBlock->uwCurrCount) {
			sliceCopEmitWait(pBlock, uwWaitX, uwStartY + y - 1, &ubWasLimitY);
		}
		for(i = 0; i < n; ++i) {
			UBYTE ubReg;
			UWORD uwColor;
			fileReadBytes(pFile, &ubReg, 1);
			uwColor = sunnyReadWord(pFile);
			copMove(pCopList, pBlock, &g_pCustom->color[ubReg], uwColor);
		}
	}

	memFree(pCounts, uwLines);
	fileClose(pFile);
	return pBlock;
}
