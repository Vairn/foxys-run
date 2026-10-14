#ifndef HUD_H
#define HUD_H

#include <ace/types.h>
#include <ace/utils/extview.h>
#include <ace/utils/bitmap.h>
#include <ace/managers/copper.h>

/**
 * SunnyLand HUD — a status overlay drawn with Amiga hardware sprites on top of
 * the scrolling playfield. It has NO viewport of its own: the sprites float
 * above the play area, so the whole screen stays available for the level.
 *
 * Contents:
 *   - up to 5 hearts (start with 3) on the left, sprite channels 0 & 1
 *   - fox-face lives icon + remaining count on channel 6 (icon then digit,
 *     same layout as the gem/cherry gauges)
 *   - gem remaining and cherry ammo stacked in channels 2 & 3 (same 16px
 *     sprites: icons on 2, two-digit counts on 3, one gauge per row)
 *
 * Channels 0–3 are reused below the HUD for the 32×32 player fox: each HUD
 * sprite DMA stream is chained to a second sprite (16-color attached pairs,
 * four channels total). Pip shots on 4–5; lives stay on 6. Sprite colours
 * 25–27 stay the pip palette; the fox is remapped onto 17–21 so the two
 * do not clash.
 *
 * Amiga sprites are 16px wide, 2BPP (3 colours + transparent), or 16-color
 * when an odd channel is attached to the even one beside it. Colours come
 * from the upper half of the palette. Those registers are set via copper,
 * since the 4BPP playfield only defines colours 0-15.
 *
 * Lifecycle (called from gamestate):
 *   hudCreate(pView) - create the sprite manager, build the glyph sprites, set
 *                      the sprite palette and enable sprite DMA. Call during
 *                      gamestate setup (before systemUnuse()).
 *   ...per frame:
 *     hudUpdate()    - redraw heart/lives/digit sprite data when values changed
 *     hudSetFoxFrame / hudSetFoxPos - player writes the chained fox
 *     hudProcess()   - refresh sprite metadata + channels (before copProcessBlocks)
 *   hudDestroy()     - disable sprite DMA and free everything.
 */

#define HUD_FOX_W 32
#define HUD_FOX_H 32
// First view-Y the chained fox may occupy (HUD_Y + gauge height).
#define HUD_FOX_MIN_Y 21

// A fox frame in ready-to-DMA sprite form: four channel blocks (one per chained
// sprite), each HUD_FOX_H rows of one 2BPP interleaved 16px line.
#define HUD_FOX_SPR_ROW_BYTES 4
#define HUD_FOX_SPR_CHANNEL_BYTES (HUD_FOX_H * HUD_FOX_SPR_ROW_BYTES)
#define HUD_FOX_SPR_FRAME_BYTES (4 * HUD_FOX_SPR_CHANNEL_BYTES)

// Stamp COLOR17–21 to the walking-fox palette. Must run inside the sky
// copper stream (a sibling block after that list never executes).
void hudEmitFoxPalette(tCopList *pList, tCopBlock *pBlock);

// Create the sprite-based HUD overlay on the given view.
void hudCreate(tView *pView);

// Redraw heart, lives, ammo, or gem sprites if those values changed.
void hudUpdate(void);

/**
 * Convert one 32×32 4bpp interleaved frame (top row uwOffsY in pSrc) into
 * HUD_FOX_SPR_FRAME_BYTES of sprite-format data.
 *
 * This shuffles 1024 pixels a bit at a time — far too slow to run on the frame
 * an animation advances (it cost roughly a third of a PAL frame, which showed
 * up as a hitch every 4th frame while running). Call it at load time for every
 * frame, then hand the results to hudSetFoxFrame().
 */
void hudBuildFoxFrame(const tBitMap *pSrc, UWORD uwOffsY, UBYTE *pDst);

// Point the chained fox at a frame built by hudBuildFoxFrame().
void hudSetFoxFrame(const UBYTE *pFrame);

// Place or hide the chained 32×32 fox. wX/wY are view-relative pixels.
void hudSetFoxPos(WORD wX, WORD wY, UBYTE isEnabled);

// Refresh sprite metadata and channels. Call every frame, right before
// copProcessBlocks().
void hudProcess(void);

// Disable sprite DMA and free all HUD sprites/bitmaps.
void hudDestroy(void);

#endif // HUD_H
