#ifndef MUSIC_H
#define MUSIC_H

#include <ace/types.h>

/**
 * PTPlayer music for the title screen and in-game levels.
 * Levels use the 3-channel PF tracks (silent 4th channel left for SFX).
 * The title screen plays the 4-channel walkyandtalky kit.
 * Samples live in ACE sample packs so the PF tracks share one kit.
 * Must run while the OS is in use (disk IO + Chip alloc).
 */

void musicStartTitle(void);
void musicStartForLevel(void);
void musicStop(void);

/** Next screen destroy leaves the current track running through the load card. */
void musicKeepPlaying(void);

/** 1 if the last musicKeepPlaying() has not been consumed by a destroy yet. */
UBYTE musicConsumeKeep(void);

#endif
