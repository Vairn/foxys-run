/* Sign triggers: trailer entities over painted 2x3 posts. Fire to read. */
#include "signs.h"
#include "level.h"
#include "overlay.h"
#include "player.h"
#include "game_defs.h"

static const char * const s_pLines[SIGN_MSG_COUNT][SIGN_LINE_MAX] = {
	{"FIRE TO SPIT", 0, 0},
	{"JUMP ON HEADS", 0, 0},
	{"CLIMB THE POLES", 0, 0},
	{"SWING FROM VINES", 0, 0},
	{"BOUNCE ON PADS", 0, 0},
	{"GEMS HOLD THE SUN", 0, 0},
	{"CHERRIES - AMMO", "GEMS OPEN THE WAY", 0}
};

static UBYTE signsClampId(UBYTE ubId) {
	if(ubId >= SIGN_MSG_COUNT) {
		ubId = 0;
	}
	return ubId;
}

UBYTE signsGetLineCount(UBYTE ubId) {
	UBYTE i = 0;
	ubId = signsClampId(ubId);
	while(i < SIGN_LINE_MAX && s_pLines[ubId][i]) {
		++i;
	}
	return i ? i : 1;
}

const char *signsGetLine(UBYTE ubId, UBYTE ubRow) {
	ubId = signsClampId(ubId);
	if(ubRow >= SIGN_LINE_MAX || !s_pLines[ubId][ubRow]) {
		return "";
	}
	return s_pLines[ubId][ubRow];
}

void signsProcess(void) {
	UWORD uwCount = levelGetSignCount();
	if(!uwCount) {
		return;
	}

	WORD wPX, wPY;
	UWORD uwPW, uwPH;
	playerGetHitbox(&wPX, &wPY, &uwPW, &uwPH);
	WORD wPR = wPX + (WORD)uwPW - 1;
	WORD wPB = wPY + (WORD)uwPH - 1;

	const tLevelEntity *pSigns = levelGetSigns();
	for(UWORD i = 0; i < uwCount; ++i) {
		// tile_y is the stamp's top row — the grass the fox stands on.
		// The 2x3 box grows up from that row so a planted post is readable.
		WORD wSX = (WORD)(pSigns[i].uwTileX << TILE_SHIFT);
		WORD wSR = wSX + (SIGN_W_TILES << TILE_SHIFT) - 1;
		WORD wSB = (WORD)((pSigns[i].uwTileY + 1) << TILE_SHIFT) - 1;
		WORD wSY = wSB - (SIGN_H_TILES << TILE_SHIFT) + 1;
		if(wSY < 0) {
			wSY = 0;
		}
		if(wSX > wPR || wSR < wPX || wSY > wPB || wSB < wPY) {
			continue;
		}
		if(!playerWantsFire()) {
			return;
		}
		playerFreeze();
		overlayBeginSign(pSigns[i].ubType);
		return;
	}
}
