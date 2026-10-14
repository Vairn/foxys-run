#include "assets.h"

#include <ace/managers/blit.h>
#include "serial_log.h"
#include <ace/utils/disk_file.h>
#include <ace/utils/pak_file.h>
#include <ace/utils/palette.h>

static tPakFile *s_pPak;

void sunnyPakInit(void) {
	s_pPak = pakFileOpen("data.pak", 1);
	if(s_pPak) {
		logWrite("pak: data.pak open (%hu files)\n", s_pPak->uwFileCount);
	}
	else {
		logWrite("pak: data.pak missing — using loose data/ files\n");
	}
}

void sunnyPakDestroy(void) {
	if(s_pPak) {
		pakFileClose(s_pPak);
		s_pPak = 0;
	}
}

UBYTE sunnyFileExists(const char *szPath) {
	if(s_pPak) {
		ULONG ulHash = pakFileGetPathHash(szPath);
		for(UWORD i = 0; i < s_pPak->uwFileCount; ++i) {
			if(s_pPak->pEntries[i].ulPathChecksum == ulHash) {
				return 1;
			}
		}
	}
	return diskFileExists(szPath);
}

tFile *sunnyOpenFile(const char *szPath) {
	/* Finish blits before OS disk access / pak seeks. */
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			return pSub;
		}
	}
	return diskFileOpen(szPath, DISK_FILE_MODE_READ, 1);
}

tBitMap *sunnyLoadBitmap(const char *szPath, UBYTE isFast) {
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			return bitmapCreateFromFd(pSub, isFast);
		}
	}
	return bitmapCreateFromPath(szPath, isFast);
}

void sunnyLoadPalette(const char *szPath, UWORD *pPalette, UWORD uwMaxLength) {
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			paletteLoadFromFd(pSub, pPalette, uwMaxLength);
			return;
		}
	}
	paletteLoadFromPath(szPath, pPalette, uwMaxLength);
}

tPtplayerMod *sunnyLoadMod(const char *szPath) {
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			return ptplayerModCreateFromFd(pSub);
		}
	}
	return ptplayerModCreateFromPath(szPath);
}

tPtplayerSamplePack *sunnyLoadSamplePack(const char *szPath) {
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			return ptplayerSampleDataCreateFromFd(pSub);
		}
	}
	return ptplayerSampleDataCreateFromPath(szPath);
}

tPtplayerSfx *sunnyLoadSfx(const char *szPath) {
	blitWait();
	if(s_pPak) {
		tFile *pSub = pakFileGetFileByPath(s_pPak, szPath);
		if(pSub) {
			return ptplayerSfxCreateFromFd(pSub, 0);
		}
	}
	return ptplayerSfxCreateFromPath(szPath, 0);
}

UWORD sunnyReadWord(tFile *pFile) {
	UWORD uwValue = 0;
	fileReadWords(pFile, &uwValue, 1);
	return uwValue;
}

void sunnyReadTiles(
	tFile *pFile, tTileBufferTileIndex *pDest, UWORD uwCount
) {
	fileReadWords(pFile, (UWORD*)pDest, uwCount);
}
