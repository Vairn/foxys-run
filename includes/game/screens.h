#ifndef SCREENS_H
#define SCREENS_H

#include <ace/managers/state.h>

extern tState *g_pIntroState;
extern tState *g_pInsertState;
extern tState *g_pTitleState;
extern tState *g_pGameState;
extern tState *g_pGameOverState;
extern tState *g_pGameWonState;

extern UBYTE g_ubIntroLaunchGame;

void introGsCreate(void);
void introGsLoop(void);
void introGsDestroy(void);

void insertGsCreate(void);
void insertGsLoop(void);
void insertGsDestroy(void);

void splashGsCreate(void);
void splashGsLoop(void);
void splashGsDestroy(void);

void titleGsCreate(void);
void titleGsLoop(void);
void titleGsDestroy(void);

void gameOverGsCreate(void);
void gameOverGsLoop(void);
void gameOverGsDestroy(void);

void gameWonGsCreate(void);
void gameWonGsLoop(void);
void gameWonGsDestroy(void);

#endif // SCREENS_H
