#ifndef SUNNY_SERIAL_LOG_H
#define SUNNY_SERIAL_LOG_H

#include <ace/managers/log.h>

/**
 * Game-only serial sink. ACE_DEBUG_SERIAL cannot run without ACE_DEBUG, and
 * ACE_DEBUG also compiles blit-range checks, alloc traces, and logBlock spam.
 * This path keeps logWrite() on the RS-232 port (115200 8N1) without that.
 *
 * When ACE_DEBUG is on, ACE's logger wins — do not enable both.
 */
void sunnySerialOpen(void);
void sunnySerialClose(void);

#if defined(SUNNY_SERIAL_LOG) && !defined(ACE_DEBUG)
void sunnyLogWrite(char *szFormat, ...) __attribute__((format(printf, 1, 2)));
#ifdef logWrite
#undef logWrite
#endif
#define logWrite(...) sunnyLogWrite(__VA_ARGS__)
#endif

#endif /* SUNNY_SERIAL_LOG_H */
