#include "game/session.h"

static UBYTE s_ubLives = SESSION_START_LIVES;
static UWORD s_uwAmmo;
static UWORD s_uwAmmoAtLevelStart;
static UBYTE s_ubDeathRestart;

void sessionNewGame(void) {
	s_ubLives = SESSION_START_LIVES;
	s_uwAmmo = 0;
	s_uwAmmoAtLevelStart = 0;
	s_ubDeathRestart = 0;
}

void sessionBeginLevel(void) {
	if(s_ubDeathRestart) {
		s_uwAmmo = s_uwAmmoAtLevelStart;
		s_ubDeathRestart = 0;
	}
	else {
		s_uwAmmoAtLevelStart = s_uwAmmo;
	}
}

UWORD sessionGetAmmo(void) {
	return s_uwAmmo;
}

void sessionSetAmmo(UWORD uwAmmo) {
	s_uwAmmo = uwAmmo;
}

UBYTE sessionGetLives(void) {
	return s_ubLives;
}

UBYTE sessionOnDeath(void) {
	if(s_ubLives) {
		--s_ubLives;
	}
	if(!s_ubLives) {
		return 0;
	}
	s_ubDeathRestart = 1;
	return 1;
}
