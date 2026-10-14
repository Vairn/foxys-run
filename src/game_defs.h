#ifndef GAME_DEFS_H
#define GAME_DEFS_H

#include <ace/types.h>

/**
 * Shared 50 Hz playfield constants.
 *
 * 8.8 fixed point is LONG pixels: PX(3) is 3 pixels, TO_PX() truncates toward 0.
 * Tiles are 16×16. The PAL playfield is 320×256. Most maps are 15 tiles
 * tall (no Y scroll). A taller map can scroll in Y; chip for that window
 * is allocated only on that level.
 */

#define FIX_SHIFT 8
#define PX(x) ((LONG)(x) << FIX_SHIFT)
#define TO_PX(x) ((WORD)((x) >> FIX_SHIFT))

#define TILE_SHIFT 4
#define TILE_SIZE (1 << TILE_SHIFT)

#define SCREEN_W 320
#define SCREEN_H 256

/* Draw/AI cull: a bit larger than the playfield so things pop in off-screen. */
#define CULL_MARGIN 32

#endif /* GAME_DEFS_H */
