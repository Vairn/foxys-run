#ifndef SKY_H
#define SKY_H

#include <ace/utils/extview.h>
#include <ace/managers/viewport/scrollbuffer.h>

/**
 * Packed copper colour0 gradient for the playfield.
 *
 * If the catalog row has a SLCE v1 .cop (convention: data/levels/levelN.cop),
 * that table is the ramp — colour 0 only; registers 16–31 stay with the HUD,
 * fox, and pips. Otherwise a built-in dithered lerp is used.
 * The fox sprite palette is stamped into the same stream at HUD_FOX_MIN_Y.
 */

void skyCreate(tView *pView, const tVPort *pVPort);

/** Tall maps: point the baked sky around the scroll break. Flat maps do nothing. */
void skySync(tScrollBufferManager *pScroll);

/** Put block command pointers back before the copper list is freed. */
void skyDetach(void);

/** Stop the COLOR00 stream so a palette fade is not overwritten every line. */
void skyDisable(void);

/** Resume the COLOR00 stream after a fade. */
void skyEnable(void);

#endif /* SKY_H */
