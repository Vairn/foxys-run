#ifndef SUNNY_ASSETS_H
#define SUNNY_ASSETS_H

#include <ace/types.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/file.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/viewport/tilebuffer.h>

/**
 * ACE pak + loose-file fallback (TwinWorld-style).
 * Prefer data.pak when present; otherwise open data/... from disk (HD/debug).
 */

void sunnyPakInit(void);
void sunnyPakDestroy(void);

/** Open path from pak or disk. Caller must fileClose(). */
tFile *sunnyOpenFile(const char *szPath);

/** Quiet exists check (pak hash walk, then disk). Does not log misses. */
UBYTE sunnyFileExists(const char *szPath);

tBitMap *sunnyLoadBitmap(const char *szPath, UBYTE isFast);
void sunnyLoadPalette(const char *szPath, UWORD *pPalette, UWORD uwMaxLength);
tPtplayerMod *sunnyLoadMod(const char *szPath);
tPtplayerSamplePack *sunnyLoadSamplePack(const char *szPath);
tPtplayerSfx *sunnyLoadSfx(const char *szPath);

/**
 * The .dat files written by tools/ store multi-byte fields big-endian.
 * These wrap ACE's endian-aware fileReadWords().
 */

/** Read one big-endian UWORD, returned in native byte order. */
UWORD sunnyReadWord(tFile *pFile);

/** Read uwCount big-endian tile indices into pDest in native byte order. */
void sunnyReadTiles(
	tFile *pFile, tTileBufferTileIndex *pDest, UWORD uwCount
);

#endif
