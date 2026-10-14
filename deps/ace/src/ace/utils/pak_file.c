/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ace/utils/pak_file.h>
#include <string.h>
#include <ace/utils/disk_file.h>
#include <ace/utils/lz.h>
#include <ace/managers/memory.h>
#include <ace/managers/log.h>

#if !defined(ACE_FILE_USE_ONLY_DISK)
#define ADLER32_MODULO 65521

typedef struct tPakFileSubfileData {
	tPakFile *pPak;
	tPakFileEntry *pEntry;
	ULONG ulPos;
} tPakFileSubfileData;

typedef struct tPakFileUnpackedData {
	UBYTE *pRaw;
	ULONG ulSize;
	ULONG ulPos;
} tPakFileUnpackedData;

static void pakSubfileClose(void *pData);
static ULONG pakSubfileRead(void *pData, void *pDest, ULONG ulSize);
static ULONG pakSubfileWrite(UNUSED_ARG void *pData, UNUSED_ARG const void *pSrc, UNUSED_ARG ULONG ulSize);
static ULONG pakSubfileSeek(void *pData, LONG lPos, WORD wMode);
static ULONG pakSubfileGetPos(void *pData);
static ULONG pakSubfileGetSize(void *pData);
static UBYTE pakSubfileIsEof(void *pData);
static void pakSubfileFlush(UNUSED_ARG void *pData);

static void pakUnpackedClose(void *pData);
static ULONG pakUnpackedRead(void *pData, void *pDest, ULONG ulSize);
static ULONG pakUnpackedWrite(UNUSED_ARG void *pData, UNUSED_ARG const void *pSrc, UNUSED_ARG ULONG ulSize);
static ULONG pakUnpackedSeek(void *pData, LONG lPos, WORD wMode);
static ULONG pakUnpackedGetPos(void *pData);
static ULONG pakUnpackedGetSize(void *pData);
static UBYTE pakUnpackedIsEof(void *pData);
static void pakUnpackedFlush(UNUSED_ARG void *pData);

static const tFileCallbacks s_sPakSubfileCallbacks = {
	.cbFileClose = pakSubfileClose,
	.cbFileRead = pakSubfileRead,
	.cbFileWrite = pakSubfileWrite,
	.cbFileSeek = pakSubfileSeek,
	.cbFileGetPos = pakSubfileGetPos,
	.cbFileGetSize = pakSubfileGetSize,
	.cbFileIsEof = pakSubfileIsEof,
	.cbFileFlush = pakSubfileFlush,
};

static const tFileCallbacks s_sPakUnpackedCallbacks = {
	.cbFileClose = pakUnpackedClose,
	.cbFileRead = pakUnpackedRead,
	.cbFileWrite = pakUnpackedWrite,
	.cbFileSeek = pakUnpackedSeek,
	.cbFileGetPos = pakUnpackedGetPos,
	.cbFileGetSize = pakUnpackedGetSize,
	.cbFileIsEof = pakUnpackedIsEof,
	.cbFileFlush = pakUnpackedFlush,
};

//------------------------------------------------------------------ PRIVATE FNS

static ULONG adler32Buffer(const UBYTE *pData, ULONG ulDataSize) {
	ULONG a = 1, b = 0;
	for(ULONG i = 0; i < ulDataSize; ++i) {
		a += pData[i];
		if(a >= ADLER32_MODULO) {
			a -= ADLER32_MODULO;
		}
		b += a;
		if(b >= ADLER32_MODULO) {
			b -= ADLER32_MODULO;
		}
	}
	return (b << 16) | a;
}

static void pakSubfileClose(void *pData) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;

	memFree(pSubfileData, sizeof(*pSubfileData));
}

static ULONG pakSubfileRead(void *pData, void *pDest, ULONG ulSize) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;
	tPakFile *pPak = pSubfileData->pPak;

	if(pPak->pPrevReadSubfile != pSubfileData) {
		// Other file was accessed or there was a seek recently - seek back
		fileSeek(
			pPak->pFile,
			pSubfileData->pEntry->ulOffs + pSubfileData->ulPos,
			FILE_SEEK_SET
		);
		pPak->pPrevReadSubfile = pSubfileData;
	}

	// Enforce upper bound of the file size
	ULONG ulRemaining = pSubfileData->pEntry->ulSizeData - pSubfileData->ulPos;
	if(ulRemaining < ulSize) {
		ulSize = ulRemaining;
		if(!ulSize) {
			return 0;
		}
	}

	ULONG ulRead = fileReadBytes(pPak->pFile, (UBYTE*)pDest, ulSize);
	pSubfileData->ulPos += ulRead;
	return ulRead;
}

static ULONG pakSubfileWrite(
	UNUSED_ARG void *pData, UNUSED_ARG const void *pSrc, UNUSED_ARG ULONG ulSize
) {
	logWrite("ERR: Unsupported: pakSubfileWrite()\n");
	return 0;
}

static ULONG pakSubfileSeek(void *pData, LONG lPos, WORD wMode) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;
	ULONG ulSizeData = pSubfileData->pEntry->ulSizeData;

	if(wMode == FILE_SEEK_SET) {
		pSubfileData->ulPos = lPos;
	}
	else if(wMode == FILE_SEEK_CURRENT) {
		pSubfileData->ulPos += lPos;
	}
	else if(wMode == FILE_SEEK_END) {
		pSubfileData->ulPos = ulSizeData + lPos;
	}
	pSubfileData->pPak->pPrevReadSubfile = 0; // Invalidate cache

	if(pSubfileData->ulPos > ulSizeData) {
		logWrite("ERR: Seek position %lu out of range %lu for pakFile data %p\n",
			pSubfileData->ulPos, ulSizeData, pSubfileData
		);
		pSubfileData->ulPos = ulSizeData;
		return 0;
	}
	return 1;
}

static ULONG pakSubfileGetPos(void *pData) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;

	return pSubfileData->ulPos;
}

static ULONG pakSubfileGetSize(void *pData) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;

	return pSubfileData->pEntry->ulSizeData;
}

static UBYTE pakSubfileIsEof(void *pData) {
	tPakFileSubfileData *pSubfileData = (tPakFileSubfileData*)pData;

	return pSubfileData->ulPos >= pakSubfileGetSize(pData);
}

static void pakSubfileFlush(UNUSED_ARG void *pData) {
	// no-op
}

static void pakUnpackedClose(void *pData) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	if(pUnpacked->pRaw) {
		memFree(pUnpacked->pRaw, pUnpacked->ulSize);
	}
	memFree(pUnpacked, sizeof(*pUnpacked));
}

static ULONG pakUnpackedRead(void *pData, void *pDest, ULONG ulSize) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	ULONG ulLeft = pUnpacked->ulSize - pUnpacked->ulPos;
	if(ulSize > ulLeft) {
		ulSize = ulLeft;
	}
	if(!ulSize) {
		return 0;
	}
	memcpy(pDest, pUnpacked->pRaw + pUnpacked->ulPos, ulSize);
	pUnpacked->ulPos += ulSize;
	return ulSize;
}

static ULONG pakUnpackedWrite(
	UNUSED_ARG void *pData, UNUSED_ARG const void *pSrc, UNUSED_ARG ULONG ulSize
) {
	logWrite("ERR: Unsupported: pakUnpackedWrite()\n");
	return 0;
}

static ULONG pakUnpackedSeek(void *pData, LONG lPos, WORD wMode) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	ULONG ulSize = pUnpacked->ulSize;
	ULONG ulPosTarget;

	if(wMode == SEEK_CUR) {
		ulPosTarget = pUnpacked->ulPos + (ULONG)lPos;
	}
	else if(wMode == SEEK_END) {
		ulPosTarget = ulSize + (ULONG)lPos;
	}
	else {
		ulPosTarget = (ULONG)lPos;
	}

	if(ulPosTarget > ulSize) {
		logWrite(
			"ERR: Seek position %lu out of range %lu for unpacked pakFile\n",
			ulPosTarget, ulSize
		);
		return 0;
	}
	pUnpacked->ulPos = ulPosTarget;
	return 1;
}

static ULONG pakUnpackedGetPos(void *pData) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	return pUnpacked->ulPos;
}

static ULONG pakUnpackedGetSize(void *pData) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	return pUnpacked->ulSize;
}

static UBYTE pakUnpackedIsEof(void *pData) {
	tPakFileUnpackedData *pUnpacked = (tPakFileUnpackedData*)pData;
	return pUnpacked->ulPos >= pUnpacked->ulSize;
}

static void pakUnpackedFlush(UNUSED_ARG void *pData) {
	// no-op
}

static UWORD pakFileGetFileIndexByHash(const tPakFile *pPakFile, ULONG ulPathHash) {
	for(UWORD i = 0; i < pPakFile->uwFileCount; ++i) {
		if(pPakFile->pEntries[i].ulPathChecksum == ulPathHash) {
			return i;
		}
	}
	return UWORD_MAX;
}

//------------------------------------------------------------------- PUBLIC FNS

tPakFile *pakFileOpen(const char *szPath, UBYTE isUninterrupted) {
	logBlockBegin("pakFileOpen(szPath: '%s')", szPath);
	tFile *pMainFile = diskFileOpen(szPath, DISK_FILE_MODE_READ, isUninterrupted);
	if(!pMainFile) {
		logBlockEnd("pakFileOpen()");
		return 0;
	}

	tPakFile *pPakFile = memAllocFast(sizeof(*pPakFile));
	pPakFile->pFile = pMainFile;
	pPakFile->pPrevReadSubfile = 0;
	fileReadWords(pMainFile, &pPakFile->uwFileCount, 1);
	pPakFile->pEntries = memAllocFast(sizeof(pPakFile->pEntries[0]) * pPakFile->uwFileCount);
	for(UWORD i = 0; i < pPakFile->uwFileCount; ++i) {
		fileReadLongs(pMainFile, &pPakFile->pEntries[i].ulPathChecksum, 1);
		fileReadLongs(pMainFile, &pPakFile->pEntries[i].ulOffs, 1);
		fileReadLongs(pMainFile, &pPakFile->pEntries[i].ulSizeUncompressed, 1);
		fileReadLongs(pMainFile, &pPakFile->pEntries[i].ulSizeData, 1);
	}
	logWrite("Pak file: %p, file count: %hu\n", pPakFile, pPakFile->uwFileCount);

	logBlockEnd("pakFileOpen()");
	return pPakFile;
}

void pakFileClose(tPakFile *pPakFile) {
	logBlockBegin("pakFileClose(pPakFile: %p)", pPakFile);
	fileClose(pPakFile->pFile);
	memFree(pPakFile->pEntries, sizeof(pPakFile->pEntries[0]) * pPakFile->uwFileCount);
	memFree(pPakFile, sizeof(*pPakFile));
	logBlockEnd("pakFileClose()");
}

tFile *pakFileGetFileByPath(tPakFile *pPakFile, const char *szInternalPath) {
	logBlockBegin("pakFileGetFileByPath(pPakFile: %p, szInternalPath: '%s')", pPakFile, szInternalPath);
	tFile *pFile = pakFileGetFileByHash(pPakFile, pakFileGetPathHash(szInternalPath));
	logBlockEnd("pakFileGetFileByPath()");
	return pFile;
}

tFile *pakFileGetFileByHash(tPakFile *pPakFile, ULONG ulPathHash) {
	logBlockBegin("pakFileGetFileByHash(pPakFile: %p, ulPathHash: 0x%08lX)", pPakFile, ulPathHash);
	tFile *pFile = pakFileGetFileByIndex(pPakFile, pakFileGetFileIndexByHash(pPakFile, ulPathHash));
	logBlockEnd("pakFileGetFileByHash()");
	return pFile;
}

tFile *pakFileGetFileByIndex(tPakFile *pPakFile, UWORD uwFileIndex) {
	logBlockBegin("pakFileGetFileByIndex(pPakFile: %p, uwFileIndex: %hu)", pPakFile, uwFileIndex);
	if(uwFileIndex >= pPakFile->uwFileCount) {
		logWrite("ERR: File index %hu out of range %hu\n", uwFileIndex, pPakFile->uwFileCount);
		logBlockEnd("pakFileGetFileByIndex()");
		return 0;
	}

	if(uwFileIndex == UWORD_MAX) {
		logWrite("ERR: Can't find subfile in pakfile\n");
		logBlockEnd("pakFileGetFileByIndex()");
		return 0;
	}
	UBYTE isCompressed = pPakFile->pEntries[uwFileIndex].ulSizeUncompressed != pPakFile->pEntries[uwFileIndex].ulSizeData;
	logWrite(
		"Subfile index: %hu, offset: %lu, size: %lu, compressed: %hhu\n",
		uwFileIndex,
		pPakFile->pEntries[uwFileIndex].ulOffs,
		pPakFile->pEntries[uwFileIndex].ulSizeUncompressed,
		isCompressed
	);

	// Create tFile, fill subfileData
	tPakFileSubfileData *pSubfileData = memAllocFast(sizeof(*pSubfileData));
	pSubfileData->pPak = pPakFile;
	pSubfileData->pEntry = &pPakFile->pEntries[uwFileIndex];
	pSubfileData->ulPos = 0;
	// Prevent reading from same place if pSubfileData gets mem from recently closed file
	pPakFile->pPrevReadSubfile = 0;

	tFile *pFile = memAllocFast(sizeof(*pFile));
	pFile->pCallbacks = &s_sPakSubfileCallbacks;
	pFile->pData = pSubfileData;

	if(isCompressed) {
		ULONG ulCompSize = pPakFile->pEntries[uwFileIndex].ulSizeData;
		ULONG ulRawSize = pPakFile->pEntries[uwFileIndex].ulSizeUncompressed;
		UBYTE *pComp = 0;
		UBYTE *pRaw = 0;
		tPakFileUnpackedData *pUnpacked;

		pComp = memAllocFast(ulCompSize);
		pRaw = memAllocFast(ulRawSize);
		if(!pComp || !pRaw) {
			logWrite("ERR: lz one-shot alloc failed for subfile %hu\n", uwFileIndex);
			if(pComp) {
				memFree(pComp, ulCompSize);
			}
			if(pRaw) {
				memFree(pRaw, ulRawSize);
			}
			fileClose(pFile);
			logBlockEnd("pakFileGetFileByIndex()");
			return 0;
		}
		if(fileReadBytes(pFile, pComp, ulCompSize) != ulCompSize) {
			logWrite("ERR: lz compressed read failed for subfile %hu\n", uwFileIndex);
			memFree(pComp, ulCompSize);
			memFree(pRaw, ulRawSize);
			fileClose(pFile);
			logBlockEnd("pakFileGetFileByIndex()");
			return 0;
		}
		pPakFile->pPrevReadSubfile = 0;
		fileClose(pFile);
		if(lzUnpack(pComp, ulCompSize, pRaw, ulRawSize) != ulRawSize) {
			logWrite("ERR: lz unpack failed for subfile %hu\n", uwFileIndex);
			memFree(pComp, ulCompSize);
			memFree(pRaw, ulRawSize);
			logBlockEnd("pakFileGetFileByIndex()");
			return 0;
		}
		memFree(pComp, ulCompSize);

		pUnpacked = memAllocFast(sizeof(*pUnpacked));
		pUnpacked->pRaw = pRaw;
		pUnpacked->ulSize = ulRawSize;
		pUnpacked->ulPos = 0;

		pFile = memAllocFast(sizeof(*pFile));
		pFile->pCallbacks = &s_sPakUnpackedCallbacks;
		pFile->pData = pUnpacked;
	}

	logBlockEnd("pakFileGetFileByIndex()");
	return pFile;
}

ULONG pakFileGetPathHash(const char *szPath) {
	return adler32Buffer((UBYTE*)szPath, strlen(szPath)); // TODO: Calculate path hash
}

#endif
