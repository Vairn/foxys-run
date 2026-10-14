#include "sfx.h"
#include "assets.h"

#include "serial_log.h"
#include <ace/managers/ptplayer.h>

#define SFX_CHANNEL 3

typedef enum tSfxId {
	SFX_JUMP,
	SFX_MONSTER_DIE,
	SFX_GEM,
	SFX_HURT,
	SFX_SPIT,
	SFX_SPIT2,
	SFX_SWITCH_ON,
	SFX_SWITCH_OFF,
	SFX_DOOR,
	SFX_COUNT
} tSfxId;

static const char *const s_pPaths[SFX_COUNT] = {
	"data/audio/jump.sfx",
	"data/audio/monster_die.sfx",
	"data/audio/gem.sfx",
	"data/audio/hurt.sfx",
	"data/audio/spit.sfx",
	"data/audio/spit2.sfx",
	"data/audio/switch_on.sfx",
	"data/audio/switch_off.sfx",
	"data/audio/door.sfx",
};

static tPtplayerSfx *s_pSfx[SFX_COUNT];
static UBYTE s_ubSpitAlt;
static UBYTE s_ubLoaded;

static void sfxPlay(tSfxId eId, UBYTE ubPriority) {
	if(eId >= SFX_COUNT || !s_pSfx[eId]) {
		return;
	}
	ptplayerSfxPlay(s_pSfx[eId], SFX_CHANNEL, PTPLAYER_VOLUME_MAX, ubPriority);
}

void sfxLoad(void) {
	if(s_ubLoaded) {
		return;
	}
	s_ubSpitAlt = 0;
	for(UBYTE i = 0; i < SFX_COUNT; ++i) {
		s_pSfx[i] = sunnyLoadSfx(s_pPaths[i]);
		if(!s_pSfx[i]) {
			logWrite("ERROR: sfx load failed (%s)\n", s_pPaths[i]);
		}
	}
	s_ubLoaded = 1;
}

void sfxDestroy(void) {
	for(UBYTE i = 0; i < SFX_COUNT; ++i) {
		if(s_pSfx[i]) {
			ptplayerSfxDestroy(s_pSfx[i]);
			s_pSfx[i] = 0;
		}
	}
	s_ubLoaded = 0;
}

void sfxPlayJump(void) {
	sfxPlay(SFX_JUMP, 8);
}

void sfxPlaySpit(void) {
	tSfxId eId = s_ubSpitAlt ? SFX_SPIT2 : SFX_SPIT;
	s_ubSpitAlt = (UBYTE)!s_ubSpitAlt;
	sfxPlay(eId, 12);
}

void sfxPlayGem(void) {
	sfxPlay(SFX_GEM, 10);
}

void sfxPlayHurt(void) {
	sfxPlay(SFX_HURT, 20);
}

void sfxPlayMonsterDie(void) {
	sfxPlay(SFX_MONSTER_DIE, 15);
}

void sfxPlaySwitchOn(void) {
	sfxPlay(SFX_SWITCH_ON, 14);
}

void sfxPlaySwitchOff(void) {
	sfxPlay(SFX_SWITCH_OFF, 14);
}

void sfxPlayDoor(void) {
	sfxPlay(SFX_DOOR, 13);
}
