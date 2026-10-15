/* End-of-level result card. Chrome and stats are drawn once. The title wave is
   a band clear plus one cookie blit per letter from glyphs baked at card
   creation. Plotting those glyphs every frame, or blitting them from an odd
   X, both lose: the plot walks every scaled pixel, and an unaligned
   interleaved blit uses an odd byte pointer and shreds the glyph. */
#include "ui/overlay.h"
#include "ui/font.h"
#include "props/signs.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/viewport/scrollbuffer.h>
#include <ace/managers/viewport/tilebuffer.h>
#include <ace/utils/bitmap.h>

#include "game/game_defs.h"

#define OVERLAY_W 192
#define OVERLAY_H 80
#define OVERLAY_VIEW_Y 48

#define COL_BG 1
#define COL_BORDER 8
#define COL_TITLE 8
#define COL_LABEL 12
#define COL_NUM 5

#define OVERLAY_STATS_FRAME 28
#define OVERLAY_SKIP_FRAME 50
#define OVERLAY_END_FRAME 180
#define OVERLAY_SIGN_SKIP_FRAME 8

#define OVERLAY_KIND_COMPLETE 0
#define OVERLAY_KIND_SIGN 1

#define TITLE_SCALE 2
#define TITLE_BASE_Y 10
#define TITLE_GLYPH_H (FONT_H * TITLE_SCALE)
#define TITLE_ADV (FONT_ADV * TITLE_SCALE)
#define TITLE_BAND_Y (TITLE_BASE_Y - 7)
#define TITLE_BAND_H (7 * 2 + TITLE_GLYPH_H)
/* Glyphs are 10px wide on a 12px advance, so a letter can cross a 16px
   boundary. A 32px slot covers that and stays a multiple of 16, which is
   the only X the interleaved blitter copies without a shift. */
#define TITLE_SLOT_W 32

static const BYTE s_pBounce[16] = {
	0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3
};

static const char s_szTitle[] = "LEVEL COMPLETE";
#define TITLE_LETTERS (sizeof(s_szTitle) - 1)

static tBitMap *s_pBm;
static tBitMap *s_pTitleSheet;
static tBitMap *s_pTitleMask;
static WORD s_wTitleX;
static WORD s_pTitleX[TITLE_LETTERS];

static UBYTE s_ubActive;
static UBYTE s_ubFinished;
static UBYTE s_ubKind;
static UBYTE s_ubStatsDrawn;
static UWORD s_uwTick;
static UWORD s_uwCherries;
static UWORD s_uwGems;
static UWORD s_uwKills;

static void fmtNum(char *sz, UWORD uw) {
	if(uw > 999) {
		uw = 999;
	}
	UWORD uwH = uw / 100;
	UWORD uwT = (uw / 10) % 10;
	UWORD uwO = uw % 10;
	UBYTE i = 0;
	if(uwH) {
		sz[i++] = (char)('0' + uwH);
	}
	if(uwH || uwT) {
		sz[i++] = (char)('0' + uwT);
	}
	sz[i++] = (char)('0' + uwO);
	sz[i] = 0;
}

static void drawChrome(void) {
	blitRect(s_pBm, 0, 0, OVERLAY_W, OVERLAY_H, COL_BG);
	blitRect(s_pBm, 0, 0, OVERLAY_W, 1, COL_BORDER);
	blitRect(s_pBm, 0, OVERLAY_H - 1, OVERLAY_W, 1, COL_BORDER);
	blitRect(s_pBm, 0, 0, 1, OVERLAY_H, COL_BORDER);
	blitRect(s_pBm, OVERLAY_W - 1, 0, 1, OVERLAY_H, COL_BORDER);
}

static void drawStatLine(WORD wY, const char *szLabel, UWORD uwValue) {
	fontDrawText(s_pBm, 16, wY, szLabel, COL_LABEL, 1);
	char szNum[4];
	fmtNum(szNum, uwValue);
	UWORD uwNumW = fontTextWidth(szNum, 1);
	fontDrawText(s_pBm, (WORD)(OVERLAY_W - 16 - uwNumW), wY, szNum, COL_NUM, 1);
}

static void drawStats(void) {
	blitWait();
	drawStatLine(42, "CHERRIES", s_uwCherries);
	drawStatLine(54, "GEMS", s_uwGems);
	drawStatLine(66, "ENEMIES", s_uwKills);
}

static void titleFreeBaked(void) {
	if(s_pTitleSheet) {
		bitmapDestroy(s_pTitleSheet);
		s_pTitleSheet = 0;
	}
	if(s_pTitleMask) {
		bitmapDestroy(s_pTitleMask);
		s_pTitleMask = 0;
	}
}

/* One 32px-wide row per letter, pixels already shifted to that letter's X
   inside the word. The mask repeats the ink in every plane so a cookie
   blit can stamp colour 8 without touching the background. */
static void titleBake(void) {
	UWORD uwH = (UWORD)TITLE_LETTERS * TITLE_GLYPH_H;
	UBYTE i;
	WORD wX = s_wTitleX;

	titleFreeBaked();
	for(i = 0; i < TITLE_LETTERS; ++i) {
		s_pTitleX[i] = wX;
		wX += TITLE_ADV;
	}
	s_pTitleSheet = bitmapCreate(
		TITLE_SLOT_W, uwH, 4, BMF_CLEAR | BMF_INTERLEAVED
	);
	s_pTitleMask = bitmapCreate(
		TITLE_SLOT_W, uwH, 4, BMF_CLEAR | BMF_INTERLEAVED
	);
	if(!s_pTitleSheet || !s_pTitleMask) {
		titleFreeBaked();
		return;
	}
	for(i = 0; i < TITLE_LETTERS; ++i) {
		WORD wShift = (WORD)(s_pTitleX[i] & 15);
		UWORD uwY = (UWORD)i * TITLE_GLYPH_H;
		if(s_szTitle[i] == ' ') {
			continue;
		}
		fontDrawChar(
			s_pTitleSheet, wShift, (WORD)uwY, s_szTitle[i], COL_TITLE, TITLE_SCALE
		);
		fontDrawChar(
			s_pTitleMask, wShift, (WORD)uwY, s_szTitle[i], 15, TITLE_SCALE
		);
	}
}

static void drawTitleCpu(void) {
	WORD wX = s_wTitleX;
	UBYTE i = 0;
	while(s_szTitle[i]) {
		WORD wY = (WORD)(
			TITLE_BASE_Y + s_pBounce[(s_uwTick + (UWORD)i * 2) & 15]
		);
		fontDrawChar(s_pBm, wX, wY, s_szTitle[i], COL_TITLE, TITLE_SCALE);
		wX += TITLE_ADV;
		++i;
	}
}

static void drawTitle(void) {
	UBYTE i;
	/* Leave the side borders and the stats block untouched. */
	blitRect(s_pBm, 1, TITLE_BAND_Y, OVERLAY_W - 2, TITLE_BAND_H, COL_BG);
	if(!s_pTitleSheet || !s_pTitleMask) {
		blitWait();
		drawTitleCpu();
		return;
	}
	for(i = 0; i < TITLE_LETTERS; ++i) {
		WORD wDstX;
		WORD wY;
		if(s_szTitle[i] == ' ') {
			continue;
		}
		wDstX = (WORD)(s_pTitleX[i] & ~15);
		wY = (WORD)(TITLE_BASE_Y + s_pBounce[(s_uwTick + (UWORD)i * 2) & 15]);
		blitCopyMask(
			s_pTitleSheet, 0, (WORD)((UWORD)i * TITLE_GLYPH_H),
			s_pBm, wDstX, wY, TITLE_SLOT_W, TITLE_GLYPH_H,
			(const UBYTE *)s_pTitleMask->Planes[0]
		);
	}
}

void overlayCreate(void) {
	s_pBm = bitmapCreate(OVERLAY_W, OVERLAY_H, 4, BMF_CLEAR | BMF_INTERLEAVED);
	s_wTitleX = (WORD)((OVERLAY_W - fontTextWidth(s_szTitle, TITLE_SCALE)) / 2);
	titleBake();
	s_ubActive = 0;
	s_ubFinished = 0;
	s_ubKind = OVERLAY_KIND_COMPLETE;
	s_ubStatsDrawn = 0;
	s_uwTick = 0;
}

void overlayDestroy(void) {
	if(s_pBm) {
		bitmapDestroy(s_pBm);
		s_pBm = 0;
	}
	titleFreeBaked();
	s_ubActive = 0;
}

void overlayBeginComplete(UWORD uwCherries, UWORD uwGems, UWORD uwKills) {
	if(!s_pBm) {
		return;
	}
	s_uwCherries = uwCherries;
	s_uwGems = uwGems;
	s_uwKills = uwKills;
	s_uwTick = 0;
	s_ubFinished = 0;
	s_ubKind = OVERLAY_KIND_COMPLETE;
	s_ubStatsDrawn = 0;
	s_ubActive = 1;
	drawChrome();
}

void overlayBeginSign(UBYTE ubId) {
	if(!s_pBm) {
		return;
	}
	s_uwTick = 0;
	s_ubFinished = 0;
	s_ubKind = OVERLAY_KIND_SIGN;
	s_ubStatsDrawn = 1;
	s_ubActive = 1;
	drawChrome();
	blitWait();

	UBYTE ubLines = signsGetLineCount(ubId);
	UWORD uwGap = 4;
	UWORD uwBlock = (UWORD)ubLines * TITLE_GLYPH_H;
	if(ubLines > 1) {
		uwBlock = (UWORD)(uwBlock + (UWORD)(ubLines - 1) * uwGap);
	}
	WORD wY = (WORD)((OVERLAY_H - uwBlock) / 2);
	for(UBYTE i = 0; i < ubLines; ++i) {
		const char *sz = signsGetLine(ubId, i);
		UWORD uwW = fontTextWidth(sz, TITLE_SCALE);
		WORD wX = (WORD)((OVERLAY_W - uwW) / 2);
		fontDrawText(s_pBm, wX, wY, sz, COL_TITLE, TITLE_SCALE);
		wY = (WORD)(wY + TITLE_GLYPH_H + (WORD)uwGap);
	}
}

UBYTE overlayIsActive(void) {
	return s_ubActive;
}

void overlayCancel(void) {
	s_ubActive = 0;
	s_ubFinished = 0;
}

UBYTE overlayIsFinished(void) {
	return s_ubFinished;
}

static UBYTE overlayWantsDismiss(void) {
	return (
		keyUse(KEY_SPACE) || keyUse(KEY_RETURN) ||
		keyUse(KEY_CONTROL) || keyUse(KEY_Z) ||
		joyUse(JOY1_FIRE)
	) ? 1 : 0;
}

void overlayProcess(void) {
	if(!s_ubActive || s_ubFinished) {
		return;
	}
	++s_uwTick;
	if(s_ubKind == OVERLAY_KIND_SIGN) {
		if(s_uwTick >= OVERLAY_SIGN_SKIP_FRAME && overlayWantsDismiss()) {
			s_ubActive = 0;
		}
		return;
	}
	if(s_uwTick >= OVERLAY_END_FRAME) {
		s_ubFinished = 1;
		return;
	}
	if(s_uwTick >= OVERLAY_SKIP_FRAME && overlayWantsDismiss()) {
		s_ubFinished = 1;
	}
}

void overlayDraw(
	tBitMap *pBack, UWORD uwCamX, UWORD uwCamY, UWORD uwAvailH
) {
	if(!s_ubActive || !s_pBm || !pBack) {
		return;
	}

	if(s_ubKind == OVERLAY_KIND_COMPLETE) {
		drawTitle();
		if(s_uwTick >= OVERLAY_STATS_FRAME && !s_ubStatsDrawn) {
			drawStats();
			s_ubStatsDrawn = 1;
		}
	}

	UWORD uwWorldX = uwCamX + (SCREEN_W - OVERLAY_W) / 2;
	UWORD uwWorldY = uwCamY + OVERLAY_VIEW_Y;
	UWORD uwDstY = SCROLLBUFFER_HEIGHT_MODULO(uwWorldY, uwAvailH);
	if((UWORD)(uwDstY + OVERLAY_H) <= uwAvailH) {
		blitUnsafeCopy(
			s_pBm, 0, 0, pBack, uwWorldX, uwDstY,
			OVERLAY_W, OVERLAY_H, MINTERM_COPY
		);
	}
	else {
		UWORD uwPart = uwAvailH - uwDstY;
		blitUnsafeCopy(
			s_pBm, 0, 0, pBack, uwWorldX, uwDstY,
			OVERLAY_W, uwPart, MINTERM_COPY
		);
		blitUnsafeCopy(
			s_pBm, 0, uwPart, pBack, uwWorldX, 0,
			OVERLAY_W, OVERLAY_H - uwPart, MINTERM_COPY
		);
	}
}

void overlayRepair(struct tTileBufferManager *pTiles) {
	UWORD uwCamX, uwCamY, uwAvailH;
	UWORD uwWorldX, uwWorldY, uwDstY;
	UWORD uwX0, uwY0, uwX1, uwY1, x, y;
	tBitMap *pBack, *pFront;

	if(!pTiles || !pTiles->pScroll || !pTiles->pCamera) {
		return;
	}

	uwCamX = pTiles->pCamera->uPos.uwX;
	uwCamY = pTiles->pCamera->uPos.uwY;
	uwAvailH = pTiles->pScroll->uwBmAvailHeight;
	uwWorldX = (UWORD)(uwCamX + (SCREEN_W - OVERLAY_W) / 2);
	uwWorldY = (UWORD)(uwCamY + OVERLAY_VIEW_Y);
	uwX0 = (UWORD)(uwWorldX >> TILE_SHIFT);
	uwY0 = (UWORD)(uwWorldY >> TILE_SHIFT);
	uwX1 = (UWORD)((uwWorldX + OVERLAY_W + TILE_SIZE - 1) >> TILE_SHIFT);
	uwY1 = (UWORD)((uwWorldY + OVERLAY_H + TILE_SIZE - 1) >> TILE_SHIFT);
	if(uwX1 > pTiles->uTileBounds.uwX) {
		uwX1 = pTiles->uTileBounds.uwX;
	}
	if(uwY1 > pTiles->uTileBounds.uwY) {
		uwY1 = pTiles->uTileBounds.uwY;
	}

	for(y = uwY0; y < uwY1; ++y) {
		for(x = uwX0; x < uwX1; ++x) {
			tileBufferDrawTile(pTiles, x, y);
		}
	}
	blitWait();

	pBack = pTiles->pScroll->pBack;
	pFront = pTiles->pScroll->pFront;
	if(!pBack || !pFront) {
		return;
	}
	uwDstY = SCROLLBUFFER_HEIGHT_MODULO(uwWorldY, uwAvailH);
	if((UWORD)(uwDstY + OVERLAY_H) <= uwAvailH) {
		blitUnsafeCopy(
			pBack, uwWorldX, uwDstY, pFront, uwWorldX, uwDstY,
			OVERLAY_W, OVERLAY_H, MINTERM_COPY
		);
	}
	else {
		UWORD uwPart = (UWORD)(uwAvailH - uwDstY);
		blitUnsafeCopy(
			pBack, uwWorldX, uwDstY, pFront, uwWorldX, uwDstY,
			OVERLAY_W, uwPart, MINTERM_COPY
		);
		blitUnsafeCopy(
			pBack, uwWorldX, 0, pFront, uwWorldX, 0,
			OVERLAY_W, (UWORD)(OVERLAY_H - uwPart), MINTERM_COPY
		);
	}
}
