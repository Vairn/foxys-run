/* Level title card. One simpleBuffer, drawn before viewLoad, faded in/out. */
#include "game/loading.h"
#include "world/level.h"
#include "ui/font.h"
#include "game/game_defs.h"

#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/palette.h>
#include <string.h>

#define FADE_FRAMES 8
#define MIN_FRAMES 30
#define LINE_W 96
#define LINE_X ((SCREEN_W - LINE_W) / 2)
#define BAR_W 160
#define BAR_H 8
#define BAR_X ((SCREEN_W - BAR_W) / 2)
#define BAR_Y 176
#define COL_LINE 2
#define COL_TITLE 2
#define COL_SUB 3

static const UWORD s_pPalForest[16] = {
	0x000, 0xFFF, 0xFD4, 0x9A6,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const UWORD s_pPalWinter[16] = {
	0x000, 0xEEF, 0xADF, 0x67A,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static tView *s_pView;
static tVPort *s_pVPort;
static tBitMap *s_pBm;
static const UWORD *s_pFullPal;
static UWORD s_uwFrames;

static void fmtLevel(char *sz) {
	UBYTE ub = (UBYTE)(levelGetIndex() + 1);
	sz[0] = 'L';
	sz[1] = 'E';
	sz[2] = 'V';
	sz[3] = 'E';
	sz[4] = 'L';
	sz[5] = ' ';
	if(ub >= 10) {
		sz[6] = (char)('0' + ub / 10);
		sz[7] = (char)('0' + ub % 10);
		sz[8] = '\0';
	}
	else {
		sz[6] = (char)('0' + ub);
		sz[7] = '\0';
	}
}

static void drawCentered(
	tBitMap *pBm, WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale
) {
	WORD wX = (WORD)((SCREEN_W - fontTextWidth(sz, ubScale)) / 2);
	fontDrawText(pBm, wX, wY, sz, ubColor, ubScale);
}

static void loadingFade(UBYTE ubFrom, UBYTE ubTo) {
	UBYTE i;

	if(!s_pVPort || !s_pFullPal) {
		return;
	}
	for(i = 0; i <= FADE_FRAMES; ++i) {
		UBYTE ub;
		if(ubTo >= ubFrom) {
			ub = (UBYTE)(ubFrom + ((UWORD)(ubTo - ubFrom) * i) / FADE_FRAMES);
		}
		else {
			ub = (UBYTE)(ubFrom - ((UWORD)(ubFrom - ubTo) * i) / FADE_FRAMES);
		}
		paletteDimOcs((UWORD *)s_pFullPal, s_pVPort->pPalette, 16, ub);
		paletteDimOcs((UWORD *)s_pFullPal, g_pCustom->color, 16, ub);
		copProcessBlocks();
		vPortWaitUntilEnd(s_pVPort);
		++s_uwFrames;
	}
}

static void sunnyFade(const UWORD *pFull, tVPort *pWait, UBYTE ubFrom, UBYTE ubTo) {
	UBYTE i;

	if(!pFull || !pWait) {
		return;
	}
	for(i = 0; i <= FADE_FRAMES; ++i) {
		UBYTE ub;
		if(ubTo >= ubFrom) {
			ub = (UBYTE)(ubFrom + ((UWORD)(ubTo - ubFrom) * i) / FADE_FRAMES);
		}
		else {
			ub = (UBYTE)(ubFrom - ((UWORD)(ubFrom - ubTo) * i) / FADE_FRAMES);
		}
		paletteDimOcs((UWORD *)pFull, pWait->pPalette, 16, ub);
		paletteDimOcs((UWORD *)pFull, g_pCustom->color, 16, ub);
		copProcessBlocks();
		vPortWaitUntilEnd(pWait);
	}
}

void sunnyFadeOut(const UWORD *pFull, tVPort *pWait) {
	sunnyFade(pFull, pWait, 15, 0);
}

void sunnyFadeIn(const UWORD *pFull, tVPort *pWait) {
	sunnyFade(pFull, pWait, 0, 15);
}

static void loadingDraw(tBitMap *pBm) {
	char szLevel[10];
	const char *szTitle = levelGetTitle();

	fmtLevel(szLevel);
	if(szTitle && szTitle[0]) {
		drawCentered(pBm, 96, szLevel, COL_SUB, 1);
		blitRect(pBm, LINE_X, 114, LINE_W, 1, COL_LINE);
		blitWait();
		drawCentered(pBm, 122, szTitle, COL_TITLE, 2);
		blitRect(pBm, LINE_X, 154, LINE_W, 1, COL_LINE);
	}
	else {
		drawCentered(pBm, 120, szLevel, COL_TITLE, 2);
	}
	blitRect(pBm, BAR_X, BAR_Y, BAR_W, BAR_H, COL_LINE);
	blitRect(pBm, BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 0);
}

void loadingProgress(UBYTE ubNow, UBYTE ubOf) {
	WORD wInner;
	WORD wFill;

	if(!s_pBm || !s_pVPort) {
		return;
	}
	if(!ubOf) {
		ubOf = 1;
	}
	if(ubNow > ubOf) {
		ubNow = ubOf;
	}
	wInner = (WORD)(BAR_W - 4);
	wFill = (WORD)(((UWORD)wInner * ubNow) / ubOf);
	blitRect(s_pBm, BAR_X, BAR_Y, BAR_W, BAR_H, COL_LINE);
	blitRect(s_pBm, BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 0);
	if(wFill > 0) {
		blitRect(s_pBm, BAR_X + 2, BAR_Y + 2, wFill, BAR_H - 4, COL_TITLE);
	}
	blitWait();
	copProcessBlocks();
	vPortWaitUntilEnd(s_pVPort);
	++s_uwFrames;
}

void loadingShow(void) {
	tSimpleBufferManager *pBfr;

	loadingDestroy();
	s_uwFrames = 0;
	s_pFullPal = levelIsWinter() ? s_pPalWinter : s_pPalForest;

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
	pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
		TAG_SIMPLEBUFFER_BOUND_WIDTH, SCREEN_W,
		TAG_SIMPLEBUFFER_BOUND_HEIGHT, SCREEN_H,
		TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
		TAG_DONE
	);
	if(!s_pView || !s_pVPort || !pBfr) {
		loadingDestroy();
		return;
	}

	s_pBm = pBfr->pBack;
	loadingDraw(s_pBm);
	memcpy(s_pVPort->pPalette, s_pFullPal, 16 * sizeof(UWORD));

	viewLoad(s_pView);
	loadingFade(0, 15);
}

void loadingHide(void) {
	if(!s_pView || !s_pVPort) {
		return;
	}
	while(s_uwFrames < MIN_FRAMES) {
		copProcessBlocks();
		vPortWaitUntilEnd(s_pVPort);
		++s_uwFrames;
	}
	loadingFade(15, 0);
}

void loadingDestroy(void) {
	if(s_pView) {
		viewDestroy(s_pView);
	}
	s_pView = 0;
	s_pVPort = 0;
	s_pBm = 0;
	s_pFullPal = 0;
}
