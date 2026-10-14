#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/state.h>
#include "debug/serial_log.h"
#include "assets/assets.h"
#include "game/gamestate.h"
#include "world/level.h"
#include "game/screens.h"
#include "audio/sfx.h"

tStateManager *g_pStateManager;
tState *g_pTitleState;
tState *g_pGameState;
tState *g_pGameOverState;
tState *g_pGameWonState;

void genericCreate(void) {
	sunnySerialOpen();
	logWrite("SunnyLand starting\n");
	keyCreate();
	joyOpen();
	sunnyPakInit();
	levelCatalogLoad();
	g_pStateManager = stateManagerCreate();
	g_pTitleState = stateCreate(titleGsCreate, titleGsLoop, titleGsDestroy, 0, 0);
	g_pGameState = stateCreate(gameGsCreate, gameGsLoop, gameGsDestroy, 0, 0);
	g_pGameOverState = stateCreate(gameOverGsCreate, gameOverGsLoop, gameOverGsDestroy, 0, 0);
	g_pGameWonState = stateCreate(gameWonGsCreate, gameWonGsLoop, gameWonGsDestroy, 0, 0);
	statePush(g_pStateManager, g_pTitleState);
	logWrite("SunnyLand: title pushed\n");
}

void genericProcess(void) {
	keyProcess();
	joyProcess();
	stateProcess(g_pStateManager);
}

void genericDestroy(void) {
	stateManagerDestroy(g_pStateManager);
	stateDestroy(g_pTitleState);
	stateDestroy(g_pGameState);
	stateDestroy(g_pGameOverState);
	stateDestroy(g_pGameWonState);
	sfxDestroy();
	levelCatalogDestroy();
	sunnyPakDestroy();
	joyClose();
	keyDestroy();
	sunnySerialClose();
}
