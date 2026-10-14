#ifndef SUNNY_FONT_H
#define SUNNY_FONT_H

#include <ace/types.h>
#include <ace/utils/bitmap.h>

#define FONT_W 5
#define FONT_H 7
#define FONT_ADV (FONT_W + 1)

UWORD fontTextWidth(const char *sz, UBYTE ubScale);

void fontDrawChar(
	tBitMap *pBm, WORD wX, WORD wY, char c, UBYTE ubColor, UBYTE ubScale
);

void fontDrawText(
	tBitMap *pBm, WORD wX, WORD wY, const char *sz, UBYTE ubColor, UBYTE ubScale
);

#endif // SUNNY_FONT_H
