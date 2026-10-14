#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/state.h>
#include "debug/serial_log.h"
#include "assets/assets.h"
#include "audio/music.h"
#include "game/screens.h"

#ifndef ACE_HOST
#include <proto/dos.h>
#endif

tStateManager *g_pStateManager;
tState *g_pSplashState;
tState *g_pIntroState;
tState *g_pInsertState;

static void introLaunchGameDisk(void) {
#ifndef ACE_HOST
	static const char *const pNames[] = {
		"df0:sunnyland.exe",
		"df0:sunnyland",
		"sunnyland.exe",
		"sunnyland",
		0
	};
	const char *const *p;
	BPTR pSeg;

	for(p = pNames; *p; ++p) {
		pSeg = LoadSeg((STRPTR)*p);
		if(pSeg) {
			logWrite("intro: CreateProc %s\n", *p);
			CreateProc("sunnyland", 0, pSeg, 16384);
			return;
		}
	}
	logWrite("intro: game exe not found after swap\n");
#else
	(void)0;
#endif
}

void genericCreate(void) {
	sunnySerialOpen();
	logWrite("SunnyLand intro starting\n");
	keyCreate();
	joyOpen();
	sunnyPakInit();
	g_ubIntroLaunchGame = 0;
	g_pStateManager = stateManagerCreate();
	g_pSplashState = stateCreate(splashGsCreate, splashGsLoop, splashGsDestroy, 0, 0);
	g_pIntroState = stateCreate(introGsCreate, introGsLoop, introGsDestroy, 0, 0);
	g_pInsertState = stateCreate(insertGsCreate, insertGsLoop, insertGsDestroy, 0, 0);
	if(sunnyFileExists("data/ui/dragotech.bm")) {
		statePush(g_pStateManager, g_pSplashState);
		logWrite("SunnyLand: splash pushed\n");
	}
	else {
		statePush(g_pStateManager, g_pIntroState);
		logWrite("SunnyLand: intro pushed\n");
	}
}

void genericProcess(void) {
	keyProcess();
	joyProcess();
	stateProcess(g_pStateManager);
}

void genericDestroy(void) {
	stateManagerDestroy(g_pStateManager);
	stateDestroy(g_pSplashState);
	stateDestroy(g_pIntroState);
	stateDestroy(g_pInsertState);
	musicStop();
	sunnyPakDestroy();
	joyClose();
	keyDestroy();
	if(g_ubIntroLaunchGame) {
		introLaunchGameDisk();
	}
	sunnySerialClose();
}
