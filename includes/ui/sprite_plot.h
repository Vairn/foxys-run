#ifndef SPRITE_PLOT_H
#define SPRITE_PLOT_H

#include <ace/types.h>
#include <ace/utils/bitmap.h>

/**
 * Plot one 2BPP pixel into an ACE hardware-sprite bitmap.
 * uwY is the glyph row (0-based); bitmap row is +1 to skip the DMA header line.
 */

#define SPR_W 16
#define SPR_BYTE_WIDTH (SPR_W / 8)
#define SPR_ROW_STRIDE (SPR_BYTE_WIDTH * 2)

static inline void sprPlot(tBitMap *pBm, UWORD uwX, UWORD uwY, UBYTE ubColor) {
	ULONG ulOffs = (ULONG)(uwY + 1) * SPR_ROW_STRIDE + (uwX >> 3);
	UBYTE ubBit = (UBYTE)(0x80 >> (uwX & 7));
	UBYTE *pP0 = (UBYTE *)pBm->Planes[0] + ulOffs;
	UBYTE *pP1 = pP0 + SPR_BYTE_WIDTH;
	if(ubColor & 1) {
		*pP0 |= ubBit;
	}
	else {
		*pP0 &= (UBYTE)~ubBit;
	}
	if(ubColor & 2) {
		*pP1 |= ubBit;
	}
	else {
		*pP1 &= (UBYTE)~ubBit;
	}
}

#endif /* SPRITE_PLOT_H */
