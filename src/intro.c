#include "screens.h"
#include "assets.h"
#include "font.h"
#include "music.h"
#include "slicecop.h"

#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/game.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/custom.h>
#include <ace/utils/disk_file.h>
#include <ace/utils/extview.h>

extern tStateManager *g_pStateManager;

#define VIEW_W 320
#define INPUT_LOCK_FRAMES 8
#define INTRO_COL_SHADOW 1
#define INTRO_COL_TEXT 15
#define INTRO_COL_SHADOW_RGB 0x211
#define INTRO_COL_TEXT_RGB 0xFE8

typedef struct tIntroBoard {
	const char *szBm;
	const char *szPlt;
	const char *szCop;
	UWORD uwFrames;
	const char *szLine0;
	const char *szLine1;
	UBYTE ubScale0;
} tIntroBoard;

static const tIntroBoard s_pBoards[] = {
	{
		"data/ui/intro_grove.bm", "data/ui/intro_grove.plt",
		"data/ui/intro_grove.cop", 150, 0, 0, 1
	},
	{
		"data/ui/intro_sun.bm", "data/ui/intro_sun.plt",
		"data/ui/intro_sun.cop", 100, 0, 0, 1
	},
	{
		"data/ui/intro_break.bm", "data/ui/intro_break.plt",
		"data/ui/intro_break.cop", 125,
		"THE GROVE WENT COLD", 0, 2
	},
	{
		"data/ui/intro_shards.bm", "data/ui/intro_shards.plt",
		"data/ui/intro_shards.cop", 150,
		"THE SUN BROKE INTO GEMS.", 0, 1
	},
	{
		"data/ui/intro_door.bm", "data/ui/intro_door.plt",
		"data/ui/intro_door.cop", 125, 0, 0, 1
	},
	{
		"data/ui/intro_gather.bm", "data/ui/intro_gather.plt",
		"data/ui/intro_gather.cop", 150,
		"GATHER THEM ALL AND", "THE LIGHT COMES BACK.", 1
	},
	{
		"data/ui/intro_rival.bm", "data/ui/intro_rival.plt",
		"data/ui/intro_rival.cop", 125, 0, 0, 1
	},
};

#define INTRO_BOARD_COUNT (sizeof(s_pBoards) / sizeof(s_pBoards[0]))

static tView *s_pView;
static tVPort *s_pVPort;
static tBitMap *s_pBm;
static tCopBlock *s_pSliceBlock;
static tCopBlock *s_pPinBlock;
static UWORD s_uwFrames;
static UBYTE s_ubLock;
static UBYTE s_ubBoard;

static void introDrawCentered(
	tBitMap *pBm, WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale
) {
	WORD wX = (WORD)((VIEW_W - fontTextWidth(sz, ubScale)) / 2);

	fontDrawText(pBm, wX + 1, wY + 1, sz, INTRO_COL_SHADOW, ubScale);
	fontDrawText(pBm, wX, wY, sz, ubColor, ubScale);
}

static void introPinWait(
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

static void introPinTextColors(UWORD uwY0, UWORD uwH) {
	tCopList *pCopList;
	UWORD uwStartY, y, uwCmds;
	UBYTE ubWasLimitY;

	if(!uwH) {
		return;
	}
	pCopList = s_pView->pCopList;
	uwStartY = (UWORD)(s_pView->ubPosY + s_pVPort->uwOffsY);
	uwCmds = (UWORD)(uwH * 4 + 2);
	s_pPinBlock = copBlockCreate(pCopList, uwCmds, 0, uwStartY + uwY0);
	ubWasLimitY = ((uwStartY + uwY0) >= 0xFF) ? 1 : 0;
	for(y = 0; y < uwH; ++y) {
		if(s_pPinBlock->uwCurrCount) {
			introPinWait(s_pPinBlock, 0, uwStartY + uwY0 + y, &ubWasLimitY);
		}
		copMove(pCopList, s_pPinBlock, &g_pCustom->color[INTRO_COL_SHADOW], INTRO_COL_SHADOW_RGB);
		copMove(pCopList, s_pPinBlock, &g_pCustom->color[INTRO_COL_TEXT], INTRO_COL_TEXT_RGB);
	}
}

static void introTeardownView(void) {
	if(s_pPinBlock) {
		copBlockDestroy(s_pView->pCopList, s_pPinBlock);
		s_pPinBlock = 0;
	}
	if(s_pSliceBlock) {
		copBlockDestroy(s_pView->pCopList, s_pSliceBlock);
		s_pSliceBlock = 0;
	}
	if(s_pView) {
		viewDestroy(s_pView);
		s_pView = 0;
		s_pVPort = 0;
	}
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
}

static UBYTE introShowBoard(void) {
	const tIntroBoard *pBoard;
	tSimpleBufferManager *pBfr;
	UWORD uwTextY, uwTextH;

	while(s_ubBoard < INTRO_BOARD_COUNT) {
		pBoard = &s_pBoards[s_ubBoard];
		if(sunnyFileExists(pBoard->szBm)) {
			break;
		}
		++s_ubBoard;
	}
	if(s_ubBoard >= INTRO_BOARD_COUNT) {
		return 0;
	}
	pBoard = &s_pBoards[s_ubBoard];

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_VIEW_GLOBAL_BPP, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, 4,
		TAG_DONE
	);
	sunnyLoadPalette(pBoard->szPlt, s_pVPort->pPalette, 16);
	s_pVPort->pPalette[INTRO_COL_SHADOW] = INTRO_COL_SHADOW_RGB;
	s_pVPort->pPalette[INTRO_COL_TEXT] = INTRO_COL_TEXT_RGB;

	s_pBm = sunnyLoadBitmap(pBoard->szBm, 0);
	if(!s_pBm) {
		introTeardownView();
		++s_ubBoard;
		return introShowBoard();
	}

	uwTextY = 0;
	uwTextH = 0;
	if(pBoard->szLine0) {
		uwTextY = pBoard->szLine1 ? 184 : 198;
		introDrawCentered(
			s_pBm, (WORD)uwTextY, pBoard->szLine0, INTRO_COL_TEXT, pBoard->ubScale0
		);
		uwTextH = (UWORD)(FONT_H * pBoard->ubScale0 + 2);
		if(pBoard->szLine1) {
			introDrawCentered(
				s_pBm, (WORD)(uwTextY + uwTextH + 4), pBoard->szLine1, INTRO_COL_TEXT, 1
			);
			uwTextH = (UWORD)(uwTextH + 4 + FONT_H + 2);
		}
	}

	pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
		TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
		TAG_SIMPLEBUFFER_FRONT_BITMAP, s_pBm,
		TAG_SIMPLEBUFFER_BACK_BITMAP, s_pBm,
		TAG_DONE
	);
	s_pSliceBlock = sliceCopCreate(s_pView, s_pVPort, pBoard->szCop);
	if(uwTextH) {
		introPinTextColors(uwTextY, uwTextH);
	}

	simpleBufferProcess(pBfr);
	viewLoad(s_pView);
	s_uwFrames = pBoard->uwFrames;
	s_ubLock = INPUT_LOCK_FRAMES;
	return 1;
}

static void introGoInsert(void) {
	musicKeepPlaying();
	stateChange(g_pStateManager, g_pInsertState);
}

void introGsCreate(void) {
	s_ubBoard = 0;
	s_pView = 0;
	s_pBm = 0;
	s_pSliceBlock = 0;
	s_pPinBlock = 0;
	musicStartTitle();
	if(!introShowBoard()) {
		s_uwFrames = 1;
		s_ubLock = 0;
		return;
	}
	systemUnuse();
}

void introGsLoop(void) {
	if(!s_pView) {
		introGoInsert();
		return;
	}
	if(s_ubLock) {
		--s_ubLock;
	}
	else if(
		keyUse(KEY_SPACE) || keyUse(KEY_RETURN) || keyUse(KEY_ESCAPE) ||
		joyUse(JOY1_FIRE)
	) {
		introGoInsert();
		return;
	}
	else if(!--s_uwFrames) {
		systemUse();
		introTeardownView();
		++s_ubBoard;
		if(!introShowBoard()) {
			introGoInsert();
			return;
		}
		systemUnuse();
		return;
	}

	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void introGsDestroy(void) {
	if(s_pView) {
		systemUse();
		introTeardownView();
	}
}

UBYTE g_ubIntroLaunchGame;

static UBYTE s_ubWrongDisk;
static const char *const s_pGameNames[] = {
	"df0:sunnyland.exe",
	"df0:sunnyland",
	"sunnyland.exe",
	"sunnyland",
	0
};

static UBYTE introGameDiskPresent(void) {
	const char *const *p;

	for(p = s_pGameNames; *p; ++p) {
		if(diskFileExists(*p)) {
			return 1;
		}
	}
	return 0;
}

static void insertPaint(void) {
	blitRect(s_pBm, 0, 0, 320, 256, 0);
	introDrawCentered(s_pBm, 90, "INSERT GAME DISK", INTRO_COL_TEXT, 2);
	introDrawCentered(s_pBm, 130, "FOXYS RUN", INTRO_COL_TEXT, 1);
	if(s_ubWrongDisk) {
		introDrawCentered(s_pBm, 170, "WRONG DISK", INTRO_COL_TEXT, 1);
	}
	introDrawCentered(s_pBm, 210, "PRESS FIRE", INTRO_COL_TEXT, 1);
}

void insertGsCreate(void) {
	tSimpleBufferManager *pBfr;

	s_ubWrongDisk = 0;
	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_VIEW_GLOBAL_BPP, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, 4,
		TAG_DONE
	);
	s_pVPort->pPalette[0] = 0x003;
	s_pVPort->pPalette[INTRO_COL_SHADOW] = INTRO_COL_SHADOW_RGB;
	s_pVPort->pPalette[INTRO_COL_TEXT] = INTRO_COL_TEXT_RGB;
	s_pBm = bitmapCreate(320, 256, 4, BMF_CLEAR | BMF_INTERLEAVED);
	insertPaint();
	pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
		TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
		TAG_SIMPLEBUFFER_FRONT_BITMAP, s_pBm,
		TAG_SIMPLEBUFFER_BACK_BITMAP, s_pBm,
		TAG_DONE
	);
	simpleBufferProcess(pBfr);
	viewLoad(s_pView);
	s_ubLock = INPUT_LOCK_FRAMES;
	systemUnuse();
}

void insertGsLoop(void) {
	if(s_ubLock) {
		--s_ubLock;
	}
	else if(
		keyUse(KEY_SPACE) || keyUse(KEY_RETURN) ||
		joyUse(JOY1_FIRE)
	) {
		if(introGameDiskPresent()) {
			g_ubIntroLaunchGame = 1;
			gameExit();
			return;
		}
		s_ubWrongDisk = 1;
		insertPaint();
		s_ubLock = INPUT_LOCK_FRAMES;
	}
	else if(keyUse(KEY_ESCAPE)) {
		gameExit();
		return;
	}

	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void insertGsDestroy(void) {
	systemUse();
	if(s_pView) {
		viewDestroy(s_pView);
		s_pView = 0;
		s_pVPort = 0;
	}
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
}
