#include "music.h"
#include "assets.h"
#ifndef SUNNY_INTRO
#include "level.h"
#include "sfx.h"
#endif

#include "serial_log.h"
#include <ace/managers/ptplayer.h>
#include <ace/managers/system.h>

#define MUSIC_PATH_MAX 64

static tPtplayerMod *s_pMod;
static tPtplayerSamplePack *s_pPack;
static UBYTE s_ubActive;
static UBYTE s_ubKeep;
static char s_szModPath[MUSIC_PATH_MAX];
static char s_szPackPath[MUSIC_PATH_MAX];

static void copyPath(char *szDst, const char *szSrc) {
	UWORD i = 0;
	if(!szSrc) {
		szDst[0] = '\0';
		return;
	}
	while(szSrc[i] && i < MUSIC_PATH_MAX - 1) {
		szDst[i] = szSrc[i];
		++i;
	}
	szDst[i] = '\0';
}

static UBYTE pathsEqual(const char *szA, const char *szB) {
	UWORD i = 0;
	if(!szA || !szB) {
		return 0;
	}
	while(szA[i] && szA[i] == szB[i]) {
		++i;
	}
	return (UBYTE)(szA[i] == szB[i]);
}

void musicKeepPlaying(void) {
	s_ubKeep = 1;
}

UBYTE musicConsumeKeep(void) {
	UBYTE ub = s_ubKeep;
	s_ubKeep = 0;
	return ub;
}

void musicStop(void) {
	if(!s_ubActive) {
		return;
	}
	ptplayerStop();
	if(s_pMod) {
		ptplayerModDestroy(s_pMod);
		s_pMod = 0;
	}
	if(s_pPack) {
		ptplayerSamplePackDestroy(s_pPack);
		s_pPack = 0;
	}
	ptplayerDestroy();
	s_ubActive = 0;
	s_szModPath[0] = '\0';
	s_szPackPath[0] = '\0';
}

static void musicStart(const char *szModPath, const char *szPackPath) {
	musicStop();
	ptplayerCreate(systemIsPal());
	s_ubActive = 1;
	/* Channels 0–2 for music; channel 3 is the SFX voice. */
	ptplayerSetMusicChannelMask(0x07);
#ifndef SUNNY_INTRO
	sfxLoad();
#endif
	ptplayerConfigureSongRepeat(1, 0);
	s_pPack = sunnyLoadSamplePack(szPackPath);
	s_pMod = sunnyLoadMod(szModPath);
	if(!s_pPack || !s_pMod) {
		logWrite("ERROR: music load failed (%s / %s)\n", szModPath, szPackPath);
		return;
	}
	ptplayerLoadMod(s_pMod, s_pPack, 0);
	ptplayerSetMasterVolume(48);
	ptplayerEnableMusic(1);
	copyPath(s_szModPath, szModPath);
	copyPath(s_szPackPath, szPackPath);
}

void musicStartTitle(void) {
	const char *szMod = "data/audio/walkyandtalky.mod";
	const char *szPack = "data/audio/walky.smp";

	if(s_ubActive && pathsEqual(s_szModPath, szMod) && pathsEqual(s_szPackPath, szPack)) {
		return;
	}
	musicStart(szMod, szPack);
}

#ifndef SUNNY_INTRO
void musicStartForLevel(void) {
	const tLevelDef *pLevel = levelGetCurrent();
	const char *szMod = (pLevel && pLevel->szMusicPath[0])
		? pLevel->szMusicPath
		: "data/audio/pf_cool.mod";
	const char *szPack = "data/audio/pf.smp";
	if(s_ubActive && pathsEqual(s_szModPath, szMod) && pathsEqual(s_szPackPath, szPack)) {
		return;
	}
	musicStart(szMod, szPack);
}
#endif
