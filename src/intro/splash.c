#include "game/screens.h"
#include "assets/assets.h"
#include "intro/slicecop.h"

#include <ace/managers/copper.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>

extern tStateManager *g_pStateManager;

static tView *s_pView;
static tVPort *s_pVPort;
static tBitMap *s_pBm;
static tCopBlock *s_pSliceBlock;
static UWORD s_uwFrames;
static UBYTE s_ubLock;

void splashGsCreate(void) {
	tSimpleBufferManager *pBfr;

	s_uwFrames = 150;
	s_ubLock = 8;

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
	sunnyLoadPalette("data/ui/dragotech.plt", s_pVPort->pPalette, 16);

	s_pBm = sunnyLoadBitmap("data/ui/dragotech.bm", 0);
	pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
		TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
		TAG_SIMPLEBUFFER_FRONT_BITMAP, s_pBm,
		TAG_SIMPLEBUFFER_BACK_BITMAP, s_pBm,
		TAG_DONE
	);
	s_pSliceBlock = sliceCopCreate(s_pView, s_pVPort, "data/ui/dragotech.cop");

	simpleBufferProcess(pBfr);
	viewLoad(s_pView);
	systemUnuse();
}

void splashGsLoop(void) {
	if(s_ubLock) {
		--s_ubLock;
	}
	else if(
		!--s_uwFrames ||
		keyUse(KEY_SPACE) || keyUse(KEY_RETURN) || keyUse(KEY_ESCAPE) ||
		joyUse(JOY1_FIRE)
	) {
		stateChange(g_pStateManager, g_pIntroState);
		return;
	}

	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void splashGsDestroy(void) {
	systemUse();
	if(s_pSliceBlock) {
		copBlockDestroy(s_pView->pCopList, s_pSliceBlock);
		s_pSliceBlock = 0;
	}
	viewDestroy(s_pView);
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
}
