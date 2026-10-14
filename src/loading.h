#ifndef SUNNY_LOADING_H
#define SUNNY_LOADING_H

#include <ace/utils/extview.h>

/**
 * Per-level title card. Shown for the whole of gameGsCreate's disk IO so the
 * display never sits on the blank copper list. Fade in/out keeps the swap
 * from flashing a half-built playfield.
 */
void loadingShow(void);

/** Fill the bar (0..ubOf) and wait one frame so it is visible. No-op if hidden. */
void loadingProgress(UBYTE ubNow, UBYTE ubOf);

/** Hold until the card has been readable, then fade to black. View stays. */
void loadingHide(void);

/** Free the card. Call after loadingHide(), before later CHIP allocs. */
void loadingDestroy(void);

/** Dim pFull 15→0 into the wait port's palette and the colour registers. */
void sunnyFadeOut(const UWORD *pFull, tVPort *pWait);

/** Raise pFull 0→15 into the wait port's palette and the colour registers. */
void sunnyFadeIn(const UWORD *pFull, tVPort *pWait);

#endif /* SUNNY_LOADING_H */
