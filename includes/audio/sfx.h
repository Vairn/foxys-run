#ifndef SFX_H
#define SFX_H

/**
 * One-shot PTPlayer sound effects on Paula channel 3.
 * Level tunes are 3-channel, so this never steals a music voice in-game.
 * Samples load once on the first music start (CHIP + disk IO while OS is up).
 */

void sfxLoad(void);
void sfxDestroy(void);

void sfxPlayJump(void);
void sfxPlaySpit(void);
void sfxPlayGem(void);
void sfxPlayHurt(void);
void sfxPlayMonsterDie(void);
void sfxPlaySwitchOn(void);
void sfxPlaySwitchOff(void);
void sfxPlayDoor(void);

#endif
