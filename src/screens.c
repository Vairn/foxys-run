/* Title, game-over and you-win screens. */
#include "screens.h"
#include "level.h"
#include "assets.h"
#include "session.h"
#include "font.h"
#include "music.h"
#include "loading.h"

#include <ace/managers/system.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/memory.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/state.h>
#include <ace/utils/extview.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/custom.h>
#include <ace/utils/palette.h>
#include <ace/utils/sprite.h>
#include <hardware/dmabits.h>
#include <string.h>

extern tStateManager *g_pStateManager;

#define INPUT_LOCK_FRAMES 8
#define VIEW_W 320
#define VIEW_H 256
#define TITLE_PREFETCH 16
#define TITLE_BUF_W (VIEW_W + TITLE_PREFETCH + 16)
#define CLOUD_PERIOD 384
#define LOGO_W 288
#define LOGO_H 76
#define LOGO_Y 28
#define LOGO_X ((VIEW_W - LOGO_W) / 2)
#define COPYRIGHT_Y (LOGO_Y + LOGO_H + 6)
#define COPYRIGHT_H (FONT_H + 1)
#define TITLE_OVERLAY_H (COPYRIGHT_Y + COPYRIGHT_H - LOGO_Y)
#define TITLE_COL_SHADOW 1
#define TITLE_COL_DIM 14
#define TITLE_COL_TEXT 15
#define TITLE_COL_SKY 0x4FF
#define TITLE_COL_SEA 0x2BF
#define TITLE_HORIZON_Y 136
#define TITLE_SEA_H (VIEW_H - TITLE_HORIZON_Y)
#define CLOUD_SLICE_H 16
#define SKY_SCROLL_PX 2
#define SEA_SCROLL_PX 5
#define TEXT_BAND_Y (196 - TITLE_HORIZON_Y)
#define TEXT_BAND_H (TITLE_SEA_H - TEXT_BAND_Y)
#define TEXT_PAGE_GAP 16
#define TEXT_PAGE_COUNT 3
#define TEXT_STORY_Y (TEXT_BAND_H + TEXT_PAGE_GAP)
#define TEXT_HELP_Y (TEXT_STORY_Y + TEXT_BAND_H + TEXT_PAGE_GAP)
#define TEXT_BM_H (TEXT_HELP_Y + TEXT_BAND_H)
#define TEXT_SCROLL_PX 1
#define TEXT_DWELL_CREDITS 160
#define TEXT_DWELL_STORY 260
#define TEXT_DWELL_HELP 220

typedef enum tScreenKind {
	SCREEN_TITLE,
	SCREEN_GAME_OVER,
	SCREEN_GAME_WON
} tScreenKind;

static tView *s_pView;
static tVPort *s_pVPort;
static tVPort *s_pVPortSea;
static tSimpleBufferManager *s_pBfr;
static tSimpleBufferManager *s_pBfrSea;
static tBitMap *s_pLogo;
static tBitMap *s_pLogoMask;
static tBitMap *s_pClouds;
static tBitMap *s_pText;
static tBitMap *s_pTextMask;
static tBitMap *s_pCopyright;
static tBitMap *s_pCopyrightMask;
static tCopBlock *s_pHorizonCop;
static tCopBlock *s_pSpriteOffCop;
static ULONG *s_pBlankSprite;
static tBitMap *s_pSkyDrawnBm[2];
static tBitMap *s_pSeaDrawnBm[2];
static UWORD s_uwSkyDrawnSrc[2];
static UWORD s_uwSeaDrawnSrc[2];
static UWORD s_uwSkyScroll;
static UWORD s_uwSeaScroll;
static UWORD s_uwTextY;
static UWORD s_uwTextDwell;
static BYTE s_bTextDir;
static UBYTE s_ubTextPage;
static tScreenKind s_eKind;
static UBYTE s_ubInputLock;
static UBYTE s_ubWantStart;
static UWORD s_pScreenPal[16];

static void drawTextCentered(
	tBitMap *pBm, WORD wOriginX, WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale
) {
	WORD wX = wOriginX + (WORD)((VIEW_W - fontTextWidth(sz, ubScale)) / 2);
	fontDrawText(pBm, wX, wY, sz, ubColor, ubScale);
}

static void drawTextCenteredShadowed(
	tBitMap *pBm, WORD wOriginX, WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale
) {
	WORD wX = wOriginX + (WORD)((VIEW_W - fontTextWidth(sz, ubScale)) / 2);
	fontDrawText(pBm, wX + 1, wY + 1, sz, TITLE_COL_SHADOW, ubScale);
	fontDrawText(pBm, wX, wY, sz, ubColor, ubScale);
}

static void pollStart(void) {
	if(s_ubInputLock || s_ubWantStart) {
		return;
	}
	joyProcess();
	if(
		keyCheck(KEY_SPACE) || keyCheck(KEY_RETURN) ||
		joyCheck(JOY1_FIRE)
	) {
		s_ubWantStart = 1;
	}
}

static void drawTextCenteredShadowedMask(
	tBitMap *pBm, WORD wOriginX, WORD wY, const char *sz, UBYTE ubScale
) {
	WORD wX = wOriginX + (WORD)((VIEW_W - fontTextWidth(sz, ubScale)) / 2);
	fontDrawText(pBm, wX + 1, wY + 1, sz, 15, ubScale);
	fontDrawText(pBm, wX, wY, sz, 15, ubScale);
}

static void titleDrawnReset(void) {
	s_pSkyDrawnBm[0] = 0;
	s_pSkyDrawnBm[1] = 0;
	s_pSeaDrawnBm[0] = 0;
	s_pSeaDrawnBm[1] = 0;
	s_uwSkyDrawnSrc[0] = 0xFFFF;
	s_uwSkyDrawnSrc[1] = 0xFFFF;
	s_uwSeaDrawnSrc[0] = 0xFFFF;
	s_uwSeaDrawnSrc[1] = 0xFFFF;
}

static UBYTE titleLayerNeedsBlit(
	tBitMap **pBms, UWORD *pSrcs, tBitMap *pBm, UWORD uwSrc
) {
	UBYTE i;

	for(i = 0; i < 2; ++i) {
		if(pBms[i] == pBm) {
			if(pSrcs[i] == uwSrc) {
				return 0;
			}
			pSrcs[i] = uwSrc;
			return 1;
		}
	}
	i = pBms[0] ? 1 : 0;
	pBms[i] = pBm;
	pSrcs[i] = uwSrc;
	return 1;
}

static void titleBakeLine(WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale) {
	drawTextCenteredShadowed(s_pText, 0, wY, sz, ubColor, ubScale);
	drawTextCenteredShadowedMask(s_pTextMask, 0, wY, sz, ubScale);
}

static void titleBakeText(void) {
	s_pText = bitmapCreate(VIEW_W, TEXT_BM_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	s_pTextMask = bitmapCreate(VIEW_W, TEXT_BM_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	if(!s_pText || !s_pTextMask) {
		return;
	}

	titleBakeLine(0, "ART      ANSIMUZ", TITLE_COL_DIM, 1);
	titleBakeLine(12, "MUSIC    VAIRN", TITLE_COL_DIM, 1);
	titleBakeLine(24, "CODE     VAIRN", TITLE_COL_DIM, 1);
	titleBakeLine(42, "PRESS FIRE", TITLE_COL_TEXT, 2);

	titleBakeLine((WORD)TEXT_STORY_Y, "THE GROVE WENT COLD", TITLE_COL_TEXT, 2);
	titleBakeLine((WORD)(TEXT_STORY_Y + 20), "THE SUN BROKE INTO GEMS.", TITLE_COL_DIM, 1);
	titleBakeLine((WORD)(TEXT_STORY_Y + 30), "GATHER THEM ALL AND", TITLE_COL_DIM, 1);
	titleBakeLine((WORD)(TEXT_STORY_Y + 40), "THE LIGHT COMES BACK.", TITLE_COL_DIM, 1);

	titleBakeLine((WORD)TEXT_HELP_Y, "HOW TO PLAY", TITLE_COL_TEXT, 2);
	titleBakeLine((WORD)(TEXT_HELP_Y + 16), "JOY  WALK JUMP DUCK  FIRE SPIT", TITLE_COL_DIM, 1);
	titleBakeLine((WORD)(TEXT_HELP_Y + 28), "KEYS ARROWS WASD  SPACE JUMP", TITLE_COL_DIM, 1);
	titleBakeLine((WORD)(TEXT_HELP_Y + 40), "GEMS OPEN THE WAY", TITLE_COL_DIM, 1);
	titleBakeLine((WORD)(TEXT_HELP_Y + 50), "PRESS FIRE", TITLE_COL_TEXT, 1);
}

static const UWORD s_pTextPageY[TEXT_PAGE_COUNT] = {
	0, TEXT_STORY_Y, TEXT_HELP_Y
};

static const UWORD s_pTextDwell[TEXT_PAGE_COUNT] = {
	TEXT_DWELL_CREDITS, TEXT_DWELL_STORY, TEXT_DWELL_HELP
};

static void titleTextProcess(void) {
	UWORD uwTarget;

	if(s_uwTextDwell) {
		--s_uwTextDwell;
		return;
	}
	if(s_bTextDir > 0) {
		uwTarget = s_pTextPageY[s_ubTextPage + 1];
		s_uwTextY = (UWORD)(s_uwTextY + TEXT_SCROLL_PX);
		if(s_uwTextY >= uwTarget) {
			s_uwTextY = uwTarget;
			s_ubTextPage = (UBYTE)(s_ubTextPage + 1);
			s_uwTextDwell = s_pTextDwell[s_ubTextPage];
			if(s_ubTextPage >= TEXT_PAGE_COUNT - 1) {
				s_bTextDir = -1;
			}
		}
	}
	else {
		uwTarget = s_pTextPageY[s_ubTextPage - 1];
		if(s_uwTextY <= (UWORD)(uwTarget + TEXT_SCROLL_PX)) {
			s_uwTextY = uwTarget;
			s_ubTextPage = (UBYTE)(s_ubTextPage - 1);
			s_uwTextDwell = s_pTextDwell[s_ubTextPage];
			if(!s_ubTextPage) {
				s_bTextDir = 1;
			}
		}
		else {
			s_uwTextY = (UWORD)(s_uwTextY - TEXT_SCROLL_PX);
		}
	}
}

static void titleBakeCopyright(void) {
	s_pCopyright = bitmapCreate(VIEW_W, COPYRIGHT_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	s_pCopyrightMask = bitmapCreate(VIEW_W, COPYRIGHT_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	if(!s_pCopyright || !s_pCopyrightMask) {
		return;
	}

	drawTextCenteredShadowed(s_pCopyright, 0, 0, "(C) 2026", TITLE_COL_DIM, 1);
	drawTextCenteredShadowedMask(s_pCopyrightMask, 0, 0, "(C) 2026", 1);
}

static UBYTE screenFired(void) {
	pollStart();
	return s_ubWantStart;
}

static UWORD titleCamX(UWORD uwScroll) {
	return (UWORD)(TITLE_PREFETCH + (uwScroll & 15));
}

static UWORD titleSrcX(UWORD uwScroll) {
	return (UWORD)((uwScroll % CLOUD_PERIOD) & 0xFFF0);
}

static tBitMap *titleExpandClouds(tBitMap *pSrc) {
	tBitMap *pStrip;
	UWORD uwH;

	if(!pSrc) {
		return 0;
	}

	uwH = pSrc->Rows;
	pStrip = bitmapCreate(CLOUD_PERIOD * 2, uwH, pSrc->Depth, BMF_INTERLEAVED);
	if(!pStrip) {
		return pSrc;
	}

	for(WORD wY = 0; wY < (WORD)uwH; wY += CLOUD_SLICE_H) {
		WORD wSlice = CLOUD_SLICE_H;
		if(wY + wSlice > (WORD)uwH) {
			wSlice = (WORD)uwH - wY;
		}
		blitCopyAligned(pSrc, 0, wY, pStrip, 0, wY, CLOUD_PERIOD, wSlice);
		blitCopyAligned(
			pSrc, 0, wY, pStrip, CLOUD_PERIOD, wY, CLOUD_PERIOD, wSlice
		);
		pollStart();
	}
	bitmapDestroy(pSrc);
	return pStrip;
}

static void blitBand(
	tBitMap *pDst, UWORD uwSrc, WORD wSrcY, WORD wDstY, WORD wH
) {
	WORD wY;

	for(wY = 0; wY < wH; wY += CLOUD_SLICE_H) {
		WORD wSlice = CLOUD_SLICE_H;
		if(wY + wSlice > wH) {
			wSlice = wH - wY;
		}
		blitCopyAligned(
			s_pClouds, (WORD)uwSrc, wSrcY + wY,
			pDst, 0, wDstY + wY, TITLE_BUF_W, wSlice
		);
		pollStart();
	}
}

static void titleDrawSkyOverlays(tBitMap *pBm, UWORD uwCam, UWORD uwSrc, UBYTE ubClean) {
	WORD wLogoX = (WORD)(uwCam + LOGO_X);

	if(!ubClean && s_pClouds) {
		blitCopyAligned(
			s_pClouds, (WORD)uwSrc, LOGO_Y,
			pBm, 0, LOGO_Y, TITLE_BUF_W, TITLE_OVERLAY_H
		);
		pollStart();
	}
	if(s_pLogo) {
		if(s_pLogoMask) {
			blitCopyMask(
				s_pLogo, 0, 0, pBm, wLogoX, LOGO_Y, LOGO_W, LOGO_H,
				(const UBYTE *)s_pLogoMask->Planes[0]
			);
		}
		else {
			blitCopy(
				s_pLogo, 0, 0, pBm, wLogoX, LOGO_Y, LOGO_W, LOGO_H, MINTERM_COPY
			);
		}
		pollStart();
	}
	if(s_pCopyright && s_pCopyrightMask) {
		blitCopyMask(
			s_pCopyright, 0, 0, pBm, (WORD)uwCam, COPYRIGHT_Y,
			VIEW_W, COPYRIGHT_H,
			(const UBYTE *)s_pCopyrightMask->Planes[0]
		);
		pollStart();
	}
}

static void titleDrawSeaOverlays(tBitMap *pBm, UWORD uwCam, UWORD uwSrc, UBYTE ubClean) {
	if(!ubClean && s_pClouds) {
		blitCopyAligned(
			s_pClouds, (WORD)uwSrc, TITLE_HORIZON_Y + TEXT_BAND_Y,
			pBm, 0, TEXT_BAND_Y, TITLE_BUF_W, TEXT_BAND_H
		);
		pollStart();
	}
	if(s_pText && s_pTextMask) {
		blitCopyMask(
			s_pText, 0, (WORD)s_uwTextY, pBm, (WORD)uwCam, TEXT_BAND_Y, VIEW_W, TEXT_BAND_H,
			(const UBYTE *)s_pTextMask->Planes[0]
		);
		pollStart();
	}
}

static void titleDraw(void) {
	UWORD uwSkyCam = titleCamX(s_uwSkyScroll);
	UWORD uwSeaCam = titleCamX(s_uwSeaScroll);
	UWORD uwSkySrc = titleSrcX(s_uwSkyScroll);
	UWORD uwSeaSrc = titleSrcX(s_uwSeaScroll);
	tBitMap *pSky = s_pBfr->pBack;
	tBitMap *pSea = s_pBfrSea->pBack;
	UBYTE ubSkyBlit, ubSeaBlit;

	cameraSetCoord(s_pBfr->pCamera, uwSkyCam, 0);
	cameraSetCoord(s_pBfrSea->pCamera, uwSeaCam, 0);

	ubSkyBlit = titleLayerNeedsBlit(s_pSkyDrawnBm, s_uwSkyDrawnSrc, pSky, uwSkySrc);
	if(ubSkyBlit) {
		blitBand(pSky, uwSkySrc, 0, 0, TITLE_HORIZON_Y);
	}
	titleDrawSkyOverlays(pSky, uwSkyCam, uwSkySrc, ubSkyBlit);

	ubSeaBlit = titleLayerNeedsBlit(s_pSeaDrawnBm, s_uwSeaDrawnSrc, pSea, uwSeaSrc);
	if(ubSeaBlit) {
		blitBand(pSea, uwSeaSrc, TITLE_HORIZON_Y, 0, TITLE_SEA_H);
	}
	titleDrawSeaOverlays(pSea, uwSeaCam, uwSeaSrc, ubSeaBlit);
}

static tSimpleBufferManager *titleBufferCreate(tVPort *pVPort, UWORD uwHeight) {
	return simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_SIMPLEBUFFER_BOUND_WIDTH, TITLE_BUF_W,
		TAG_SIMPLEBUFFER_BOUND_HEIGHT, uwHeight,
		TAG_SIMPLEBUFFER_USE_X_SCROLLING, 1,
		TAG_DONE
	);
}

static void screenCreate(tScreenKind eKind) {
	s_eKind = eKind;
	s_ubInputLock = INPUT_LOCK_FRAMES;
	s_ubWantStart = 0;
	s_pLogo = 0;
	s_pLogoMask = 0;
	s_pClouds = 0;
	s_pText = 0;
	s_pTextMask = 0;
	s_pCopyright = 0;
	s_pCopyrightMask = 0;
	s_pHorizonCop = 0;
	s_pSpriteOffCop = 0;
	s_pBlankSprite = memAllocChipClear(sizeof(*s_pBlankSprite));
	s_pVPortSea = 0;
	s_pBfrSea = 0;
	s_uwSkyScroll = 0;
	s_uwSeaScroll = 0;
	s_uwTextY = 0;
	s_uwTextDwell = TEXT_DWELL_CREDITS;
	s_bTextDir = 1;
	s_ubTextPage = 0;
	titleDrawnReset();

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_VIEW_GLOBAL_BPP, 1,
		TAG_DONE
	);
	if(s_pView && s_pBlankSprite) {
		s_pSpriteOffCop = spriteDisableInCopBlockMode(
			s_pView->pCopList,
			SPRITE_0 | SPRITE_1 | SPRITE_2 | SPRITE_3 |
			SPRITE_4 | SPRITE_5 | SPRITE_6 | SPRITE_7,
			s_pBlankSprite
		);
	}

	if(eKind == SCREEN_TITLE) {
		UWORD pPal[16];

		s_pVPort = vPortCreate(0,
			TAG_VPORT_VIEW, s_pView,
			TAG_VPORT_BPP, 4,
			TAG_VPORT_HEIGHT, TITLE_HORIZON_Y,
			TAG_DONE
		);
		s_pBfr = titleBufferCreate(s_pVPort, TITLE_HORIZON_Y);

		s_pVPortSea = vPortCreate(0,
			TAG_VPORT_VIEW, s_pView,
			TAG_VPORT_BPP, 4,
			TAG_VPORT_HEIGHT, TITLE_SEA_H,
			TAG_DONE
		);
		s_pBfrSea = titleBufferCreate(s_pVPortSea, TITLE_SEA_H);

		sunnyLoadPalette("data/ui/title_logo.plt", pPal, 16);
		memcpy(s_pVPort->pPalette, pPal, sizeof(pPal));
		s_pVPort->pPalette[0] = TITLE_COL_SKY;
		memcpy(s_pScreenPal, s_pVPort->pPalette, sizeof(s_pScreenPal));

		s_pClouds = titleExpandClouds(sunnyLoadBitmap("data/ui/title_clouds.bm", 0));
		s_pLogo = sunnyLoadBitmap("data/ui/title_logo.bm", 0);
		if(s_pLogo) {
			s_pLogoMask = sunnyLoadBitmap("data/ui/title_logo_mask.bm", 0);
		}
		titleBakeText();
		titleBakeCopyright();

		{
			UWORD uwY = (UWORD)(s_pView->ubPosY + s_pVPort->uwOffsY + TITLE_HORIZON_Y);
			s_pHorizonCop = copBlockCreate(s_pView->pCopList, 1, 0, uwY);
			copMove(
				s_pView->pCopList, s_pHorizonCop,
				&g_pCustom->color[0], TITLE_COL_SEA
			);
		}

		titleDraw();
		simpleBufferProcess(s_pBfr);
		simpleBufferProcess(s_pBfrSea);
		titleDraw();
		musicStartTitle();
	}
	else {
		tBitMap *pBm;

		s_pVPort = vPortCreate(0,
			TAG_VPORT_VIEW, s_pView,
			TAG_VPORT_BPP, 4,
			TAG_DONE
		);
		s_pBfr = simpleBufferCreate(0,
			TAG_SIMPLEBUFFER_VPORT, s_pVPort,
			TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
			TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
			TAG_SIMPLEBUFFER_BOUND_WIDTH, VIEW_W,
			TAG_SIMPLEBUFFER_BOUND_HEIGHT, VIEW_H,
			TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
			TAG_DONE
		);
		pBm = s_pBfr->pBack;
		s_pVPort->pPalette[0] = 0x024;
		s_pVPort->pPalette[1] = 0xFFF;
		s_pVPort->pPalette[2] = 0xFD4;
		s_pVPort->pPalette[3] = 0xCCC;
		memcpy(s_pScreenPal, s_pVPort->pPalette, sizeof(s_pScreenPal));

		if(eKind == SCREEN_GAME_OVER) {
			drawTextCentered(pBm, 0, 90, "GAME OVER", 2, 3);
			drawTextCentered(pBm, 0, 140, "THE GROVE STAYS COLD", 3, 1);
			drawTextCentered(pBm, 0, 220, "PRESS FIRE", 1, 2);
		}
		else {
			drawTextCentered(pBm, 0, 90, "THE LIGHT CAME BACK", 2, 2);
			drawTextCentered(pBm, 0, 140, "THE GROVE IS WARM", 3, 1);
			drawTextCentered(pBm, 0, 220, "PRESS FIRE", 1, 2);
		}
	}

	systemSetDmaBit(DMAB_SPRITE, 0);
	if(s_pBlankSprite) {
		UBYTE i;
		for(i = 0; i < HARDWARE_SPRITE_CHANNEL_COUNT; ++i) {
			g_pCustom->sprpt[i] = (APTR)s_pBlankSprite;
		}
	}

	viewLoad(s_pView);
	systemUnuse();
}

static void screenFadeOut(void) {
	if(s_pHorizonCop) {
		copBlockDisable(s_pView->pCopList, s_pHorizonCop);
	}
	sunnyFadeOut(s_pScreenPal, s_pVPort);
}

static UBYTE screenHandleInput(UBYTE ubTickLock) {
	if(ubTickLock && s_ubInputLock) {
		--s_ubInputLock;
	}
	if(s_ubInputLock) {
		return 0;
	}
	if(screenFired()) {
		if(s_eKind == SCREEN_TITLE) {
			musicKeepPlaying();
			screenFadeOut();
			sessionNewGame();
			levelSetIndex(0);
			stateChange(g_pStateManager, g_pGameState);
			return 1;
		}
		screenFadeOut();
		stateChange(g_pStateManager, g_pTitleState);
		return 1;
	}

	if(keyUse(KEY_ESCAPE)) {
		if(s_eKind == SCREEN_TITLE) {
			gameExit();
		}
		else {
			screenFadeOut();
			stateChange(g_pStateManager, g_pTitleState);
		}
		return 1;
	}
	return 0;
}

static void screenLoop(void) {
	if(screenHandleInput(1)) {
		return;
	}

	if(s_eKind == SCREEN_TITLE && s_pClouds) {
		s_uwSkyScroll = (UWORD)((s_uwSkyScroll + SKY_SCROLL_PX) % CLOUD_PERIOD);
		s_uwSeaScroll = (UWORD)((s_uwSeaScroll + SEA_SCROLL_PX) % CLOUD_PERIOD);
		titleTextProcess();
		titleDraw();
		if(screenHandleInput(0)) {
			return;
		}
		simpleBufferProcess(s_pBfr);
		simpleBufferProcess(s_pBfrSea);
	}

	copProcessBlocks();
	vPortWaitForEnd(s_pVPortSea ? s_pVPortSea : s_pVPort);
}

static void screenDestroy(void) {
	systemUse();
	if(!musicConsumeKeep()) {
		musicStop();
	}
	if(s_pHorizonCop) {
		copBlockDestroy(s_pView->pCopList, s_pHorizonCop);
		s_pHorizonCop = 0;
	}
	if(s_pSpriteOffCop) {
		copBlockDestroy(s_pView->pCopList, s_pSpriteOffCop);
		s_pSpriteOffCop = 0;
	}
	if(s_pBlankSprite) {
		memFree(s_pBlankSprite, sizeof(*s_pBlankSprite));
		s_pBlankSprite = 0;
	}
	if(s_pLogoMask) {
		bitmapDestroy(s_pLogoMask);
		s_pLogoMask = 0;
	}
	if(s_pLogo) {
		bitmapDestroy(s_pLogo);
		s_pLogo = 0;
	}
	if(s_pTextMask) {
		bitmapDestroy(s_pTextMask);
		s_pTextMask = 0;
	}
	if(s_pText) {
		bitmapDestroy(s_pText);
		s_pText = 0;
	}
	if(s_pCopyrightMask) {
		bitmapDestroy(s_pCopyrightMask);
		s_pCopyrightMask = 0;
	}
	if(s_pCopyright) {
		bitmapDestroy(s_pCopyright);
		s_pCopyright = 0;
	}
	if(s_pClouds) {
		bitmapDestroy(s_pClouds);
		s_pClouds = 0;
	}
	viewDestroy(s_pView);
	s_pView = 0;
	s_pVPort = 0;
	s_pVPortSea = 0;
	s_pBfr = 0;
	s_pBfrSea = 0;
}

void titleGsCreate(void) { screenCreate(SCREEN_TITLE); }
void titleGsLoop(void) { screenLoop(); }
void titleGsDestroy(void) { screenDestroy(); }

void gameOverGsCreate(void) { screenCreate(SCREEN_GAME_OVER); }
void gameOverGsLoop(void) { screenLoop(); }
void gameOverGsDestroy(void) { screenDestroy(); }

void gameWonGsCreate(void) { screenCreate(SCREEN_GAME_WON); }
void gameWonGsLoop(void) { screenLoop(); }
void gameWonGsDestroy(void) { screenDestroy(); }
