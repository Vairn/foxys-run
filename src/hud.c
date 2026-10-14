/* SunnyLand — hardware-sprite HUD overlay.
 *
 * The HUD is drawn entirely with Amiga hardware sprites floating over the
 * playfield, so it needs no viewport of its own (the whole screen stays free
 * for the level). Channels:
 *
 *   0 : hearts 0,1 (row 0) and 3,4 (row 1)   (bank A colours 17-19)
 *   1 : heart 2 (row 0)                       (bank A colours 17-19)
 *   2 : gem + cherry icons, stacked           (bank B colours 21-23)
 *   3 : gem remaining + ammo, stacked         (bank B colours 21-23)
 *   4-5 : cherry pips (shots.c)
 *   6 : fox-face lives icon + count           (bank D colours 29-31)
 *       (channel 7 is unused: scroll fetch steals its DMA)
 *
 * Channels 0–3 are chained: after the HUD glyphs finish, DMA continues into a
 * 32×32 16-color fox (two attached pairs). ACE's sprite manager does not chain,
 * so we store the fox header/pixels in the same bitmap and set the HUD sprite
 * height to the gauge only.
 *
 * Each sprite is a 16px-wide, 2BPP interleaved bitmap with one extra blank line
 * at the top (control/header) and bottom (footer link), per ACE's sprite format.
 * We plot the little heart/digit/gem glyphs straight into the sprite bitplanes;
 * on health/score changes we just rewrite the pixels in place (the sprite keeps
 * pointing at the same bitmap, so no copper regen is needed).
 */
#include "hud.h"
#include "player.h"
#include "pickups.h"
#include "session.h"
#include "monsters.h"

#include <ace/managers/system.h>
#include <ace/managers/sprite.h>
#include <ace/managers/copper.h>
#include <ace/macros.h>
#include <ace/utils/custom.h>
#include <ace/utils/bitmap.h>
#include <string.h>
#include "sprite_plot.h"

// Glyph sizes (in pixels).
#define HEART_W 8
#define HEART_H 7
#define DIGIT_W 5
#define DIGIT_H 7
// Padded glyph width: DIGIT extends one pixel on every side for its outline.
#define DIGIT_PADDED_W (DIGIT_W + 2 * DIGIT_OUTLINE_PAD)
// Right-to-right spacing between two outlined digits (7 wide + 1 gap).
#define DIGIT_ADVANCE (DIGIT_PADDED_W + 1)
#define GEM_W 8
#define GEM_H 7
#define CHERRY_W 8
#define CHERRY_H 7
// One gauge row is tall enough for an outlined 5x7 digit; icons sit in the
// same rows so gem and cherry share the two 16px sprites, stacked in Y.
#define GAUGE_ROW_H (DIGIT_H + 2 * DIGIT_OUTLINE_PAD)
#define GAUGE_H (GAUGE_ROW_H * 2)

// On-screen positions (view pixels, measured from the top-left of the view).
#define HUD_Y 3
#define HEARTS_A_X 8    // hearts 0,1 (row 0) and 3,4 (row 1)
#define HEARTS_B_X 24   // heart 2 (row 0)
#define LIVES_FACE_X 44
#define GEM_X 272
#define DIGITS_X 286    // tens (local x0) + ones (local x DIGIT_ADVANCE)

#define FACE_W 8
#define FACE_H 11
// Lives count sits in the right half of channel 6, same 5x7 outlined glyph
// as the gem/ammo digits.
#define LIVES_DIGIT_X 8

// Chained 32×32 fox after the HUD glyphs on channels 0–3.
// Bitmap rows: HUD header, GAUGE_H HUD pixels, fox header, FOX_H pixels, footer.
#define FOX_CHAIN_PIXEL_ROW (GAUGE_H + 2)
#define FOX_CHAIN_HEADER_ROW (GAUGE_H + 1)
#define FOX_CHAIN_ROWS (GAUGE_H + HUD_FOX_H + 3)

// Sprite palette (12-bit RGB). Bank A = hearts, bank B = gem/digits,
// bank D (channel 6) = fox face + lives digit. After the HUD scanlines, colours 17-21
// become the walking fox; 25-27 stay the pips.
#define COL_HEART_FULL  0xF44
#define COL_HEART_EMPTY 0x622
#define COL_HEART_SPARE 0xFAA
#define COL_INK_WHITE   0xFFF   // digits + gem outline (colour index 1)
#define COL_GEM_BODY    0x2CE   // gem fill            (colour index 2)
#define COL_DIGIT_OUTLINE 0x000 // dark outline behind the count digits (colour index 3)
#define COL_FACE_OUTLINE 0x324  // fox outline / pupils
#define COL_FACE_FUR     0xE74  // orange fur
#define COL_FACE_LIGHT   0xFEE  // eyes + muzzle
// player.bm indices (histogram): $1 outline, $4 shade, $8 white, $9 body+hat,
// $A face. $5 gold is unused — the playfield quantizer folded the original
// orange into $9. Sprite copper uses the HUD face orange/cream for body/face
// so the walking fox matches the lives icon instead of the red BOB remap.
#define COL_FOX_OUTLINE  COL_FACE_OUTLINE
#define COL_FOX_SHADE    0xB65  // playfield $4
#define COL_FOX_WHITE    0xFFF  // playfield $8
#define COL_FOX_BODY     COL_FACE_FUR
#define COL_FOX_FACE     COL_FACE_LIGHT

static tView *s_pView;
static tSprite *s_pSprHeartsA;
static tSprite *s_pSprHeartsB;
static tSprite *s_pSprIcons;
static tSprite *s_pSprDigits;
static tSprite *s_pSprFace;
static tBitMap *s_pBmHeartsA;
static tBitMap *s_pBmHeartsB;
static tBitMap *s_pBmIcons;
static tBitMap *s_pBmDigits;
static tBitMap *s_pBmFace;
static tCopBlock *s_pPaletteBlock;
static tBitMap *s_pBmFoxChain[4];

static UBYTE s_ubLastHealth;
static UBYTE s_ubLastLives;
static UWORD s_uwLastAmmo;
static UWORD s_uwLastGemsLeft;
static UBYTE s_ubLastBossHp;
static UBYTE s_ubHudBossBar;

// 8x7 heart silhouette, MSB (bit 7) = leftmost pixel.
static const UBYTE s_pHeart[HEART_H] = {
	0x66, // 01100110
	0xFF, // 11111111
	0xFF, // 11111111
	0xFF, // 11111111
	0x7E, // 01111110
	0x3C, // 00111100
	0x18, // 00011000
};

// Compact 8px fox head so the lives digit fits beside it on the same 16px
// sprite (gem/cherry layout: icon then outlined count).
// 1 = outline, 2 = fur, 3 = cream/white.
static const UBYTE s_pFace[FACE_H][FACE_W] = {
	{0, 1, 1, 0, 0, 1, 1, 0},
	{1, 1, 2, 1, 1, 2, 1, 1},
	{1, 3, 2, 2, 2, 2, 3, 1},
	{1, 3, 2, 2, 2, 2, 3, 1},
	{1, 3, 2, 2, 2, 2, 3, 1},
	{1, 3, 3, 2, 2, 3, 3, 1},
	{1, 2, 2, 2, 2, 2, 2, 1},
	{1, 2, 3, 3, 3, 3, 2, 1},
	{1, 3, 3, 1, 1, 3, 3, 1},
	{1, 3, 3, 1, 1, 3, 3, 1},
	{0, 1, 2, 3, 3, 2, 1, 0},
};

// Each digit gets a 1px outline in every direction, so the glyph area needs
// 2 extra rows/columns (0x"padding" on all sides) inside the sprite bitmap.
#define DIGIT_OUTLINE_PAD 1
_Static_assert(HUD_FOX_MIN_Y == HUD_Y + GAUGE_H, "fox chain starts on the first line after the HUD");
static const UBYTE s_pDigits[10][DIGIT_H] = {
	{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
	{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
	{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
	{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
	{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
	{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
	{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
	{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
	{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
	{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
};

// 8x7 gem: colour 1 = white outline, colour 2 = cyan body.
static const UBYTE s_pGem[GEM_H][GEM_W] = {
	{0, 0, 0, 1, 1, 0, 0, 0},
	{0, 0, 1, 2, 2, 1, 0, 0},
	{0, 1, 2, 2, 2, 2, 1, 0},
	{1, 2, 2, 2, 2, 2, 2, 1},
	{0, 1, 2, 2, 2, 2, 1, 0},
	{0, 0, 1, 2, 2, 1, 0, 0},
	{0, 0, 0, 1, 1, 0, 0, 0},
};

// Clear only the glyph area of a sprite bitmap, leaving the header line and
// any chained fox data intact.
static void sprClearContent(tBitMap *pBm, UWORD uwGlyphHeight) {
	memset(
		(UBYTE*)pBm->Planes[0] + SPR_ROW_STRIDE, 0,
		(ULONG)uwGlyphHeight * SPR_ROW_STRIDE
	);
}

static void plotHeart(tBitMap *pBm, UWORD uwX0, UWORD uwY0, UBYTE ubColor) {
	for(UBYTE gy = 0; gy < HEART_H; ++gy) {
		UBYTE ubRow = s_pHeart[gy];
		for(UBYTE gx = 0; gx < HEART_W; ++gx) {
			if(ubRow & (0x80 >> gx)) {
				sprPlot(pBm, uwX0 + gx, uwY0 + gy, ubColor);
			}
		}
	}
}

static void plotFace(tBitMap *pBm) {
	for(UBYTE gy = 0; gy < FACE_H; ++gy) {
		for(UBYTE gx = 0; gx < FACE_W; ++gx) {
			UBYTE ubColor = s_pFace[gy][gx];
			if(ubColor) {
				sprPlot(pBm, gx, gy, ubColor);
			}
		}
	}
}

// Plot one 5x7 digit glyph with a 1px dark outline. The glyph is expanded by
// one pixel on every side (into colour 3 = dark) inside a padded region, then
// the white fill is drawn on top, giving the count digits a readable dark ring.
// The padded region is DIGIT_OUTLINE_PAD pixels bigger on every side than the
// glyph, so the top/bottom outline has real bitmap rows to land on.
static void plotDigit(
	tBitMap *pBm, UWORD uwX0, UWORD uwY0, UBYTE ubDigit,
	UBYTE ubFill, UBYTE ubOutline
) {
	const UBYTE *pRows = s_pDigits[ubDigit];
	for(UBYTE py = 0; py < DIGIT_H + 2 * DIGIT_OUTLINE_PAD; ++py) {
		int y = (int)py - DIGIT_OUTLINE_PAD; // glyph row, may be -1 or DIGIT_H
		UBYTE ubRow = (y >= 0 && y < DIGIT_H) ? pRows[y] : 0;
		for(UBYTE px = 0; px < DIGIT_W + 2 * DIGIT_OUTLINE_PAD; ++px) {
			int x = (int)px - DIGIT_OUTLINE_PAD; // glyph col, may be -1 or DIGIT_W
			UBYTE ubBit = (x >= 0 && x < DIGIT_W) ? (UBYTE)(0x10 >> x) : 0;
			UBYTE ubIsFill = (ubBit && (ubRow & ubBit)) ? 1 : 0;
			if(ubIsFill) {
				sprPlot(pBm, uwX0 + px, uwY0 + py, ubFill);
				continue;
			}
			// Dark outline: any 8-neighbour inside the glyph?
			UBYTE ubNeigh = 0;
			for(int dy = -1; dy <= 1 && !ubNeigh; ++dy) {
				int ny = y + dy;
				UBYTE ubNRow = (ny >= 0 && ny < DIGIT_H) ? pRows[ny] : 0;
				for(int dx = -1; dx <= 1; ++dx) {
					int nx = x + dx;
					if(nx >= 0 && nx < DIGIT_W && (ubNRow & (0x10 >> nx))) {
						ubNeigh = 1;
						break;
					}
				}
			}
			if(ubNeigh) {
				sprPlot(pBm, uwX0 + px, uwY0 + py, ubOutline);
			}
		}
	}
}

// 8x7 cherry: colour 1 = stem/outline, colour 2 = body (same bank as the gem).
static const UBYTE s_pCherry[CHERRY_H][CHERRY_W] = {
	{0, 0, 0, 0, 1, 1, 0, 0},
	{0, 0, 0, 1, 1, 0, 0, 0},
	{0, 0, 1, 2, 2, 1, 0, 0},
	{0, 1, 2, 2, 2, 2, 1, 0},
	{0, 1, 2, 2, 2, 1, 0, 0},
	{0, 0, 1, 2, 2, 1, 0, 0},
	{0, 0, 0, 1, 1, 0, 0, 0},
};

static void plotGem(tBitMap *pBm, UWORD uwX0, UWORD uwY0) {
	for(UBYTE gy = 0; gy < GEM_H; ++gy) {
		for(UBYTE gx = 0; gx < GEM_W; ++gx) {
			UBYTE ubColor = s_pGem[gy][gx];
			if(ubColor) {
				sprPlot(pBm, uwX0 + gx, uwY0 + gy, ubColor);
			}
		}
	}
}

static void plotCherry(tBitMap *pBm, UWORD uwX0, UWORD uwY0) {
	for(UBYTE gy = 0; gy < CHERRY_H; ++gy) {
		for(UBYTE gx = 0; gx < CHERRY_W; ++gx) {
			UBYTE ubColor = s_pCherry[gy][gx];
			if(ubColor) {
				sprPlot(pBm, uwX0 + gx, uwY0 + gy, ubColor);
			}
		}
	}
}

static void plotTwoDigits(tBitMap *pBm, UWORD uwY0, UWORD uwCount) {
	if(uwCount > 99) {
		uwCount = 99;
	}
	if(uwCount >= 10) {
		plotDigit(pBm, 0, uwY0, (UBYTE)(uwCount / 10), 1, 3);
		plotDigit(pBm, DIGIT_ADVANCE, uwY0, (UBYTE)(uwCount % 10), 1, 3);
	}
	else {
		plotDigit(pBm, 0, uwY0, (UBYTE)uwCount, 1, 3);
	}
}

static void hudDrawHearts(UBYTE ubHealth) {
	sprClearContent(s_pBmHeartsA, GAUGE_H);
	sprClearContent(s_pBmHeartsB, GAUGE_H);
	// Two rows, Zelda-style: three starting hearts on top, two extra below.
	plotHeart(s_pBmHeartsA, 0,       1, (ubHealth > 0) ? 1 : 2);
	plotHeart(s_pBmHeartsA, HEART_W, 1, (ubHealth > 1) ? 1 : 2);
	plotHeart(s_pBmHeartsB, 0,       1, (ubHealth > 2) ? 1 : 2);
	plotHeart(s_pBmHeartsA, 0,       GAUGE_ROW_H + 1, (ubHealth > 3) ? 1 : 2);
	plotHeart(s_pBmHeartsA, HEART_W, GAUGE_ROW_H + 1, (ubHealth > 4) ? 1 : 2);
}

static void hudDrawLives(UBYTE ubLives) {
	sprClearContent(s_pBmFace, FACE_H);
	plotFace(s_pBmFace);
	if(ubLives > 9) {
		ubLives = 9;
	}
	// Same glyph as gem/ammo: white fill, dark outline (bank D colour 3 / 1).
	plotDigit(s_pBmFace, LIVES_DIGIT_X, 0, ubLives, 3, 1);
}

static void hudDrawCounts(UWORD uwGemsLeft, UWORD uwAmmo) {
	sprClearContent(s_pBmDigits, GAUGE_H);
	plotTwoDigits(s_pBmDigits, 0, uwGemsLeft);
	plotTwoDigits(s_pBmDigits, GAUGE_ROW_H, uwAmmo);
}

static void hudDrawPickupIcons(void) {
	sprClearContent(s_pBmIcons, GAUGE_H);
	plotGem(s_pBmIcons, 0, 1);
	plotCherry(s_pBmIcons, 0, GAUGE_ROW_H + 1);
}

static void hudDrawBossHp(UBYTE ubHp) {
	sprClearContent(s_pBmIcons, GAUGE_H);
	sprClearContent(s_pBmDigits, GAUGE_H);
	plotHeart(s_pBmIcons, 0, 1, (ubHp > 0) ? 1 : 2);
	plotHeart(s_pBmIcons, HEART_W, 1, (ubHp > 1) ? 1 : 2);
	plotHeart(s_pBmDigits, 0, 1, (ubHp > 2) ? 1 : 2);
	plotHeart(s_pBmDigits, HEART_W, 1, (ubHp > 3) ? 1 : 2);
	plotHeart(s_pBmIcons, 0, GAUGE_ROW_H + 1, (ubHp > 4) ? 1 : 2);
}

static void hudDrawGemRemaining(UWORD uwGemsLeft) {
	sprClearContent(s_pBmIcons, GAUGE_H);
	sprClearContent(s_pBmDigits, GAUGE_H);
	plotGem(s_pBmIcons, 0, 1);
	plotTwoDigits(s_pBmDigits, 0, uwGemsLeft);
}

static tBitMap *hudCreateSpriteBitmap(UWORD uwGlyphHeight, UBYTE isChainFox) {
	UWORD uwRows = uwGlyphHeight + 2;
	if(isChainFox) {
		uwRows = FOX_CHAIN_ROWS;
	}
	return bitmapCreate(SPR_W, uwRows, 2, BMF_CLEAR | BMF_INTERLEAVED);
}

void hudEmitFoxPalette(tCopList *pList, tCopBlock *pBlock) {
	copMove(pList, pBlock, &g_pCustom->color[17], COL_FOX_OUTLINE);
	copMove(pList, pBlock, &g_pCustom->color[18], COL_FOX_SHADE);
	copMove(pList, pBlock, &g_pCustom->color[19], COL_FOX_WHITE);
	copMove(pList, pBlock, &g_pCustom->color[20], COL_FOX_BODY);
	copMove(pList, pBlock, &g_pCustom->color[21], COL_FOX_FACE);
}

void hudCreate(tView *pView) {
	s_pView = pView;

	spriteManagerCreate(pView, 0, 0);
	systemSetDmaBit(DMAB_SPRITE, 1);

	// The 4BPP playfield only defines colours 0-15, so set the sprite colour
	// registers (upper palette) with a small copper block at the top of frame.
	s_pPaletteBlock = copBlockCreate(pView->pCopList, 10, 0, 0);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[17], COL_HEART_FULL);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[18], COL_HEART_EMPTY);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[19], COL_HEART_SPARE);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[20], COL_FOX_BODY);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[21], COL_INK_WHITE);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[22], COL_GEM_BODY);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[23], COL_DIGIT_OUTLINE);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[29], COL_FACE_OUTLINE);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[30], COL_FACE_FUR);
	copMove(pView->pCopList, s_pPaletteBlock, &g_pCustom->color[31], COL_INK_WHITE);

	s_pBmHeartsA = hudCreateSpriteBitmap(GAUGE_H, 1);
	s_pBmHeartsB = hudCreateSpriteBitmap(GAUGE_H, 1);
	s_pBmIcons   = hudCreateSpriteBitmap(GAUGE_H, 1);
	s_pBmDigits  = hudCreateSpriteBitmap(GAUGE_H, 1);
	s_pBmFace    = hudCreateSpriteBitmap(FACE_H, 0);

	s_pBmFoxChain[0] = s_pBmHeartsA;
	s_pBmFoxChain[1] = s_pBmHeartsB;
	s_pBmFoxChain[2] = s_pBmIcons;
	s_pBmFoxChain[3] = s_pBmDigits;

	s_pSprHeartsA = spriteAdd(0, s_pBmHeartsA);
	s_pSprHeartsB = spriteAdd(1, s_pBmHeartsB);
	s_pSprIcons   = spriteAdd(2, s_pBmIcons);
	s_pSprDigits  = spriteAdd(3, s_pBmDigits);
	s_pSprFace    = spriteAdd(6, s_pBmFace);

	// ACE sizes the sprite from the full bitmap (HUD + chained fox). Pin the
	// displayed HUD fragment to the gauge so DMA can continue into the fox.
	spriteSetHeight(s_pSprHeartsA, GAUGE_H);
	spriteSetHeight(s_pSprHeartsB, GAUGE_H);
	spriteSetHeight(s_pSprIcons, GAUGE_H);
	spriteSetHeight(s_pSprDigits, GAUGE_H);

	s_pSprHeartsA->wX = HEARTS_A_X; s_pSprHeartsA->wY = HUD_Y;
	s_pSprHeartsB->wX = HEARTS_B_X; s_pSprHeartsB->wY = HUD_Y;
	s_pSprIcons->wX   = GEM_X;      s_pSprIcons->wY   = HUD_Y;
	s_pSprDigits->wX  = DIGITS_X;   s_pSprDigits->wY  = HUD_Y;
	s_pSprFace->wX    = LIVES_FACE_X;  s_pSprFace->wY  = HUD_Y;
	spriteRequestMetadataUpdate(s_pSprFace);

	s_ubLastHealth = playerGetHealth();
	s_ubLastLives = sessionGetLives();
	s_uwLastAmmo = pickupsGetAmmo();
	s_uwLastGemsLeft = pickupsGetGemsRemaining();
	s_ubLastBossHp = monstersGetBossHp();
	s_ubHudBossBar = 0;
	hudDrawHearts(s_ubLastHealth);
	hudDrawLives(s_ubLastLives);
	if(monstersGetBossHpMax() && monstersBossAlive()) {
		hudDrawBossHp(s_ubLastBossHp);
		s_ubHudBossBar = 1;
	}
	else if(monstersGetBossHpMax()) {
		hudDrawGemRemaining(s_uwLastGemsLeft);
	}
	else {
		hudDrawPickupIcons();
		hudDrawCounts(s_uwLastGemsLeft, s_uwLastAmmo);
	}

	// Prime sprite metadata + channels so the HUD shows from the first frame.
	hudProcess();
}

void hudUpdate(void) {
	UBYTE ubHealth = playerGetHealth();
	if(ubHealth != s_ubLastHealth) {
		s_ubLastHealth = ubHealth;
		hudDrawHearts(ubHealth);
	}

	UBYTE ubLives = sessionGetLives();
	if(ubLives != s_ubLastLives) {
		s_ubLastLives = ubLives;
		hudDrawLives(ubLives);
	}

	if(monstersGetBossHpMax()) {
		if(monstersBossAlive()) {
			UBYTE ubBossHp = monstersGetBossHp();
			if(!s_ubHudBossBar || ubBossHp != s_ubLastBossHp) {
				s_ubLastBossHp = ubBossHp;
				s_ubHudBossBar = 1;
				hudDrawBossHp(ubBossHp);
			}
		}
		else {
			UWORD uwGemsLeft = pickupsGetGemsRemaining();
			if(s_ubHudBossBar || uwGemsLeft != s_uwLastGemsLeft) {
				s_ubHudBossBar = 0;
				s_uwLastGemsLeft = uwGemsLeft;
				hudDrawGemRemaining(uwGemsLeft);
			}
		}
		return;
	}

	UWORD uwAmmo = pickupsGetAmmo();
	UWORD uwGemsLeft = pickupsGetGemsRemaining();
	if(uwAmmo != s_uwLastAmmo || uwGemsLeft != s_uwLastGemsLeft) {
		s_uwLastAmmo = uwAmmo;
		s_uwLastGemsLeft = uwGemsLeft;
		hudDrawCounts(uwGemsLeft, uwAmmo);
	}
}

// Playfield index → attached-sprite index, skipping 9-11 (COLOR25-27 = pips).
// $1 outline→1, $4 shade→2, $8 white→3, $9 body+hat→4, $A face→5.
static const UBYTE s_pFoxRemap[16] = {
	0, 1, 1, 1, 2, 2, 2, 2, 3, 4, 5, 2, 3, 5, 1, 1
};

_Static_assert(
	HUD_FOX_SPR_ROW_BYTES == SPR_ROW_STRIDE,
	"prebuilt fox rows must match the sprite bitmap stride"
);

void hudBuildFoxFrame(const tBitMap *pSrc, UWORD uwOffsY, UBYTE *pDst) {
	UWORD uwSrcBpr = pSrc->BytesPerRow;
	UWORD uwPlaneBytes = bitmapGetByteWidth(pSrc);
	const UBYTE *pSrc0 = (const UBYTE *)pSrc->Planes[0] + (ULONG)uwOffsY * uwSrcBpr;
	UWORD uwRowOff = 0;

	memset(pDst, 0, HUD_FOX_SPR_FRAME_BYTES);
	for(UWORD y = 0; y < HUD_FOX_H; ++y) {
		UBYTE pOut[4][4];
		memset(pOut, 0, sizeof(pOut));
		for(UBYTE x = 0; x < HUD_FOX_W; ++x) {
			UBYTE ubBit = (UBYTE)(0x80 >> (x & 7));
			UBYTE ubByte = (UBYTE)(x >> 3);
			UBYTE ubIdx = 0;
			if(pSrc0[ubByte] & ubBit) { ubIdx |= 1; }
			if(pSrc0[uwPlaneBytes + ubByte] & ubBit) { ubIdx |= 2; }
			if(pSrc0[2 * uwPlaneBytes + ubByte] & ubBit) { ubIdx |= 4; }
			if(pSrc0[3 * uwPlaneBytes + ubByte] & ubBit) { ubIdx |= 8; }
			ubIdx = s_pFoxRemap[ubIdx];
			if(!ubIdx) {
				continue;
			}
			UBYTE ubPair = (x >= 16) ? 2 : 0;
			UBYTE ubLocal = (UBYTE)(x & 15);
			UBYTE ubDBit = (UBYTE)(0x80 >> (ubLocal & 7));
			UBYTE ubDByte = (UBYTE)(ubLocal >> 3);
			if(ubIdx & 1) { pOut[ubPair][ubDByte] |= ubDBit; }
			if(ubIdx & 2) { pOut[ubPair][2 + ubDByte] |= ubDBit; }
			if(ubIdx & 4) { pOut[ubPair + 1][ubDByte] |= ubDBit; }
			if(ubIdx & 8) { pOut[ubPair + 1][2 + ubDByte] |= ubDBit; }
		}
		for(UBYTE c = 0; c < 4; ++c) {
			UBYTE *pOutRow = pDst + c * HUD_FOX_SPR_CHANNEL_BYTES + uwRowOff;
			pOutRow[0] = pOut[c][0];
			pOutRow[1] = pOut[c][1];
			pOutRow[2] = pOut[c][2];
			pOutRow[3] = pOut[c][3];
		}
		pSrc0 += uwSrcBpr;
		uwRowOff += HUD_FOX_SPR_ROW_BYTES;
	}
}

void hudSetFoxFrame(const UBYTE *pFrame) {
	UWORD uwDstOff = (UWORD)(FOX_CHAIN_PIXEL_ROW * SPR_ROW_STRIDE);
	for(UBYTE c = 0; c < 4; ++c) {
		memcpy(
			(UBYTE *)s_pBmFoxChain[c]->Planes[0] + uwDstOff,
			pFrame + c * HUD_FOX_SPR_CHANNEL_BYTES,
			HUD_FOX_SPR_CHANNEL_BYTES
		);
	}
}

static void hudWriteFoxHeader(
	tBitMap *pBm, WORD wX, WORD wY, UBYTE isAttached, UBYTE isEnabled
) {
	tHardwareSpriteHeader *pHeader = (tHardwareSpriteHeader *)(
		(UBYTE *)pBm->Planes[0] + (ULONG)FOX_CHAIN_HEADER_ROW * SPR_ROW_STRIDE
	);
	if(!isEnabled) {
		pHeader->uwRawPos = 0;
		pHeader->uwRawCtl = 0;
		return;
	}
	UWORD uwVStart = s_pView->ubPosY + wY;
	UWORD uwVStop = uwVStart + HUD_FOX_H;
	UWORD uwHStart = s_pView->ubPosX - 1 + wX;
	pHeader->uwRawPos = (UWORD)((uwVStart << 8) | (uwHStart >> 1));
	pHeader->uwRawCtl = (UWORD)(
		(uwVStop << 8) |
		(isAttached << 7) |
		(BTST(uwVStart, 8) << 2) |
		(BTST(uwVStop, 8) << 1) |
		BTST(uwHStart, 0)
	);
}

void hudSetFoxPos(WORD wX, WORD wY, UBYTE isEnabled) {
	hudWriteFoxHeader(s_pBmFoxChain[0], wX, wY, 0, isEnabled);
	hudWriteFoxHeader(s_pBmFoxChain[1], wX, wY, 1, isEnabled);
	hudWriteFoxHeader(s_pBmFoxChain[2], (WORD)(wX + 16), wY, 0, isEnabled);
	hudWriteFoxHeader(s_pBmFoxChain[3], (WORD)(wX + 16), wY, 1, isEnabled);
}

void hudProcess(void) {
	spriteProcess(s_pSprHeartsA);
	spriteProcess(s_pSprHeartsB);
	spriteProcess(s_pSprIcons);
	spriteProcess(s_pSprDigits);
	spriteProcess(s_pSprFace);
	spriteProcessChannel(0);
	spriteProcessChannel(1);
	spriteProcessChannel(2);
	spriteProcessChannel(3);
	spriteProcessChannel(6);
}

void hudDestroy(void) {
	systemSetDmaBit(DMAB_SPRITE, 0);

	spriteRemove(s_pSprHeartsA);
	spriteRemove(s_pSprHeartsB);
	spriteRemove(s_pSprIcons);
	spriteRemove(s_pSprDigits);
	spriteRemove(s_pSprFace);

	bitmapDestroy(s_pBmHeartsA);
	bitmapDestroy(s_pBmHeartsB);
	bitmapDestroy(s_pBmIcons);
	bitmapDestroy(s_pBmDigits);
	bitmapDestroy(s_pBmFace);

	if(s_pPaletteBlock) {
		copBlockDestroy(s_pView->pCopList, s_pPaletteBlock);
		s_pPaletteBlock = 0;
	}

	spriteManagerDestroy();
}
