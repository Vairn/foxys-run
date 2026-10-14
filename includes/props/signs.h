#ifndef SUNNY_SIGNS_H
#define SUNNY_SIGNS_H

#include <ace/types.h>

#define SIGN_MSG_COUNT 7
#define SIGN_LINE_MAX 3
#define SIGN_W_TILES 2
#define SIGN_H_TILES 3

UBYTE signsGetLineCount(UBYTE ubId);

const char *signsGetLine(UBYTE ubId, UBYTE ubRow);

void signsProcess(void);

#endif // SUNNY_SIGNS_H
