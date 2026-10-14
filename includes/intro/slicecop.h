#ifndef SLICECOP_H
#define SLICECOP_H

#include <ace/managers/copper.h>
#include <ace/utils/extview.h>

/**
 * One packed copBlock of per-scanline COLOR MOVEs from a .cop slice table.
 * Line 0 is the viewport .plt; this stream starts at the end of that line
 * so simpleBuffer's BPLPT block (startY-1) still runs first.
 * Destroy the block before viewDestroy, same as the title horizon copper.
 */
tCopBlock *sliceCopCreate(
	tView *pView, const tVPort *pVPort, const char *szPath
);

#endif
