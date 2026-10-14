#ifndef SESSION_H
#define SESSION_H

#include <ace/types.h>

/**
 * Run-long state that survives a level reload (gamestate destroy/create).
 *
 * Lives and cherry ammo live here so a death retry can restore the cherries
 * you walked in with, and so a new game from the title screen starts clean.
 */

#define SESSION_START_LIVES 3

// Fresh run: 3 lives, no cherries. Call when leaving the title screen.
void sessionNewGame(void);

// Start (or restart) a level. On a death retry this restores the cherry
// count from when the level was first entered; otherwise it snapshots the
// current count so a later death can roll it back.
void sessionBeginLevel(void);

UWORD sessionGetAmmo(void);
void sessionSetAmmo(UWORD uwAmmo);

UBYTE sessionGetLives(void);

// Spend one life. Returns 1 if the player still has a life left to retry
// the current level; 0 means game over.
UBYTE sessionOnDeath(void);

#endif // SESSION_H
