/* RS-232 logWrite() without ACE_DEBUG. See serial_log.h. */
#include "serial_log.h"

#if defined(SUNNY_SERIAL_LOG) && !defined(ACE_DEBUG)

#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <stdarg.h>
#include <stdio.h>

static UBYTE s_ubOpen;
static char s_szMsg[512];

#ifndef ACE_HOST
/* Don't use misc.resource: Kick 3.1 serial.device often already owns the
 * port, AllocMiscResource fails, and we would send nothing. The game has
 * already taken the machine; poke Paula like ACE_DEBUG_SERIAL does. */
static void printSerial(const char *szMsg) {
	const char *pChar = szMsg;
	while(*pChar) {
		UWORD uwSpin = 0xFFFF;
		while((g_pCustom->serdatr & SERDATRF_TBE) == 0) {
			if(--uwSpin == 0) {
				return;
			}
		}
		g_pCustom->serdat = ((UWORD)1 << 8) | (UWORD)(UBYTE)*pChar++;
	}
}
#endif

void sunnySerialOpen(void) {
	if(s_ubOpen) {
		return;
	}
#ifndef ACE_HOST
	g_pCustom->serper = SERPER(115200);
#endif
	s_ubOpen = 1;
	sunnyLogWrite("SunnyLand serial log (no ACE_DEBUG)\n");
}

void sunnySerialClose(void) {
	if(!s_ubOpen) {
		return;
	}
	sunnyLogWrite("SunnyLand serial log closed\n");
	s_ubOpen = 0;
}

void sunnyLogWrite(char *szFormat, ...) {
	va_list vaArgs;

	if(!s_ubOpen || !szFormat) {
		return;
	}
	va_start(vaArgs, szFormat);
	vsprintf(s_szMsg, szFormat, vaArgs);
	va_end(vaArgs);
#ifdef ACE_HOST
	fputs(s_szMsg, stdout);
	fflush(stdout);
#else
	printSerial(s_szMsg);
#endif
}

#else

void sunnySerialOpen(void) {
}

void sunnySerialClose(void) {
}

#endif
