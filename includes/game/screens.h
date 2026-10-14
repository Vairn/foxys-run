#ifndef SCREENS_H
#define SCREENS_H

#include <ace/managers/state.h>

extern tState *g_pTitleState;
extern tState *g_pGameState;
extern tState *g_pGameOverState;
extern tState *g_pGameWonState;

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
