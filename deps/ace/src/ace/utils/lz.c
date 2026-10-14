/* LZ4-style unpacker: 4K window, BE 12-bit offsets, unbounded match length.
 *
 * Hot path is lzUnpack(): one-shot into a caller buffer. Tuned for 68000:
 * aligned long copies, RLE fill, expanding overlap copy. Odd addresses stay
 * byte-only — MOVE.W/MOVE.L on an odd pointer is an address error on 68000.
 *
 * lzDecRead() is the streaming fallback (pak_tool / tiny reads). Same codec.
 */

#include <ace/utils/lz.h>
#include <string.h>
#include <stdint.h>

#if defined(__GNUC__)
#define LZ_INLINE static inline __attribute__((always_inline))
#define LZ_HOT __attribute__((hot))
#define LZ_LIKELY(x) __builtin_expect(!!(x), 1)
#define LZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LZ_INLINE static
#define LZ_HOT
#define LZ_LIKELY(x) (x)
#define LZ_UNLIKELY(x) (x)
#endif

#define LZ_ODD(p) (((uintptr_t)(p)) & 1u)

/* Forward copy. 68000-safe: long copies only when both pointers are even. */
LZ_INLINE void lzCopy(uint8_t *pDst, const uint8_t *pSrc, uint32_t ulN) {
	if(ulN >= 8 && !LZ_ODD(pDst) && !LZ_ODD(pSrc)) {
		while(ulN >= 16) {
			*(uint32_t *)(pDst + 0) = *(const uint32_t *)(pSrc + 0);
			*(uint32_t *)(pDst + 4) = *(const uint32_t *)(pSrc + 4);
			*(uint32_t *)(pDst + 8) = *(const uint32_t *)(pSrc + 8);
			*(uint32_t *)(pDst + 12) = *(const uint32_t *)(pSrc + 12);
			pDst += 16;
			pSrc += 16;
			ulN -= 16;
		}
		while(ulN >= 4) {
			*(uint32_t *)pDst = *(const uint32_t *)pSrc;
			pDst += 4;
			pSrc += 4;
			ulN -= 4;
		}
	}
	else {
		while(ulN >= 8) {
			pDst[0] = pSrc[0];
			pDst[1] = pSrc[1];
			pDst[2] = pSrc[2];
			pDst[3] = pSrc[3];
			pDst[4] = pSrc[4];
			pDst[5] = pSrc[5];
			pDst[6] = pSrc[6];
			pDst[7] = pSrc[7];
			pDst += 8;
			pSrc += 8;
			ulN -= 8;
		}
	}
	while(ulN--) {
		*pDst++ = *pSrc++;
	}
}

/* Byte fill. Align, then splat longs. */
LZ_INLINE void lzFill(uint8_t *pDst, uint8_t ub, uint32_t ulN) {
	if(ulN >= 8) {
		uint32_t ulSplat;
		if(LZ_ODD(pDst)) {
			*pDst++ = ub;
			ulN--;
		}
		ulSplat = (uint32_t)ub * 0x01010101u;
		while(ulN >= 16) {
			*(uint32_t *)(pDst + 0) = ulSplat;
			*(uint32_t *)(pDst + 4) = ulSplat;
			*(uint32_t *)(pDst + 8) = ulSplat;
			*(uint32_t *)(pDst + 12) = ulSplat;
			pDst += 16;
			ulN -= 16;
		}
		while(ulN >= 4) {
			*(uint32_t *)pDst = ulSplat;
			pDst += 4;
			ulN -= 4;
		}
	}
	while(ulN--) {
		*pDst++ = ub;
	}
}

/* Word-period fill (offset 2). */
LZ_INLINE void lzFill2(uint8_t *pDst, const uint8_t *pSrc, uint32_t ulN) {
	uint8_t ub0 = pSrc[0];
	uint8_t ub1 = pSrc[1];
	if(ulN >= 8 && !LZ_ODD(pDst)) {
		uint32_t ulSplat;
		uint8_t pPat[4];
		pPat[0] = ub0;
		pPat[1] = ub1;
		pPat[2] = ub0;
		pPat[3] = ub1;
		memcpy(&ulSplat, pPat, 4);
		while(ulN >= 4) {
			*(uint32_t *)pDst = ulSplat;
			pDst += 4;
			ulN -= 4;
		}
	}
	while(ulN >= 2) {
		pDst[0] = ub0;
		pDst[1] = ub1;
		pDst += 2;
		ulN -= 2;
	}
	if(ulN) {
		*pDst = ub0;
	}
}

/* Match copy. off >= n is a disjoint memcpy; off == 1/2 is a fill;
 * overlapping runs expand by doubling so each memcpy is disjoint. */
LZ_INLINE void lzMatch(uint8_t *pDst, uint32_t ulOff, uint32_t ulN) {
	const uint8_t *pSrc = pDst - ulOff;
	if(ulOff >= ulN) {
		lzCopy(pDst, pSrc, ulN);
		return;
	}
	if(ulOff == 1) {
		lzFill(pDst, pSrc[0], ulN);
		return;
	}
	if(ulOff == 2) {
		lzFill2(pDst, pSrc, ulN);
		return;
	}
	lzCopy(pDst, pSrc, ulOff);
	{
		uint32_t ulHave = ulOff;
		pDst += ulOff;
		ulN -= ulOff;
		while(ulN) {
			uint32_t ulChunk = (ulHave < ulN) ? ulHave : ulN;
			lzCopy(pDst, pDst - ulHave, ulChunk);
			pDst += ulChunk;
			ulN -= ulChunk;
			ulHave += ulChunk;
		}
	}
}

LZ_INLINE int lzExtra(
	const uint8_t **pSrc, const uint8_t *pSrcEnd, uint32_t *pLen
) {
	uint8_t ub;
	do {
		if(LZ_UNLIKELY(*pSrc >= pSrcEnd)) {
			return 0;
		}
		ub = *(*pSrc)++;
		*pLen += ub;
	} while(ub == 255);
	return 1;
}

uint32_t LZ_HOT lzUnpack(
	const uint8_t *pSrc, uint32_t ulSrcSize,
	uint8_t *pDst, uint32_t ulDstSize
) {
	const uint8_t *pSrcEnd = pSrc + ulSrcSize;
	uint8_t *pDstStart = pDst;
	uint8_t *pDstEnd = pDst + ulDstSize;

	if(LZ_UNLIKELY(!ulDstSize || !pSrc || !pDst)) {
		return 0;
	}

	while(pSrc < pSrcEnd) {
		uint8_t ubToken = *pSrc++;
		uint32_t ulLit = (uint32_t)(ubToken >> 4);
		uint32_t ulLeft;

		if(ulLit == 15) {
			if(LZ_UNLIKELY(!lzExtra(&pSrc, pSrcEnd, &ulLit))) {
				return 0;
			}
		}

		ulLeft = (uint32_t)(pDstEnd - pDst);
		if(LZ_UNLIKELY(ulLit > ulLeft || ulLit > (uint32_t)(pSrcEnd - pSrc))) {
			return 0;
		}
		if(ulLit) {
			lzCopy(pDst, pSrc, ulLit);
			pDst += ulLit;
			pSrc += ulLit;
		}

		if(pDst == pDstEnd) {
			return (pSrc == pSrcEnd) ? ulDstSize : 0;
		}
		if(LZ_UNLIKELY((uint32_t)(pSrcEnd - pSrc) < 2)) {
			return 0;
		}

		{
			uint32_t ulOffset = ((uint32_t)pSrc[0] << 8) | (uint32_t)pSrc[1];
			uint32_t ulMatch;
			pSrc += 2;
			if(LZ_UNLIKELY(
				ulOffset == 0 || ulOffset > LZ_MAX_OFFSET ||
				ulOffset > (uint32_t)(pDst - pDstStart)
			)) {
				return 0;
			}

			ulMatch = (uint32_t)(ubToken & 15) + LZ_MIN_MATCH;
			if((ubToken & 15) == 15) {
				if(LZ_UNLIKELY(!lzExtra(&pSrc, pSrcEnd, &ulMatch))) {
					return 0;
				}
			}
			if(LZ_UNLIKELY(ulMatch > (uint32_t)(pDstEnd - pDst))) {
				return 0;
			}
			lzMatch(pDst, ulOffset, ulMatch);
			pDst += ulMatch;
		}
	}

	return (pDst == pDstEnd) ? ulDstSize : 0;
}

static int lzFillIn(tLzDec *pDec) {
	uint32_t ulLeft, ulGot, ulSpace;

	if(pDec->ulInPos < pDec->ulInFill) {
		ulLeft = pDec->ulInFill - pDec->ulInPos;
		if(pDec->ulInPos) {
			memmove(pDec->pIn, pDec->pIn + pDec->ulInPos, ulLeft);
		}
		pDec->ulInFill = ulLeft;
		pDec->ulInPos = 0;
	}
	else {
		pDec->ulInFill = 0;
		pDec->ulInPos = 0;
	}

	ulSpace = pDec->ulInCap - pDec->ulInFill;
	if(!ulSpace) {
		return 1;
	}
	ulGot = pDec->cbRead(pDec->pReadUser, pDec->pIn + pDec->ulInFill, ulSpace);
	pDec->ulInFill += ulGot;
	return pDec->ulInPos < pDec->ulInFill;
}

static int lzHave(tLzDec *pDec, uint32_t ulNeed) {
	if((pDec->ulInFill - pDec->ulInPos) >= ulNeed) {
		return 1;
	}
	if(!lzFillIn(pDec)) {
		return 0;
	}
	return (pDec->ulInFill - pDec->ulInPos) >= ulNeed;
}

static uint8_t lzGet(tLzDec *pDec) {
	if(pDec->ulInPos >= pDec->ulInFill) {
		if(!lzFillIn(pDec)) {
			pDec->ubState = LZ_ST_ERR;
			return 0;
		}
	}
	return pDec->pIn[pDec->ulInPos++];
}

static int lzReadExtra(tLzDec *pDec, uint32_t *pLen) {
	uint8_t ub;
	do {
		if(!lzHave(pDec, 1)) {
			pDec->ubState = LZ_ST_ERR;
			return 0;
		}
		ub = lzGet(pDec);
		*pLen += ub;
	} while(ub == 255);
	return 1;
}

static void lzWindowWrite(tLzDec *pDec, const uint8_t *pSrc, uint32_t ulN) {
	uint32_t ulAt = pDec->ulDecoded & (LZ_WINDOW - 1);
	uint32_t ulRoom = LZ_WINDOW - ulAt;
	if(ulN <= ulRoom) {
		lzCopy(pDec->pWindow + ulAt, pSrc, ulN);
	}
	else {
		lzCopy(pDec->pWindow + ulAt, pSrc, ulRoom);
		lzCopy(pDec->pWindow, pSrc + ulRoom, ulN - ulRoom);
	}
	pDec->ulDecoded += ulN;
}

static uint32_t lzEmitLit(
	tLzDec *pDec, uint8_t *pDst, uint32_t ulOut, uint32_t ulWant
) {
	uint32_t ulAvail, ulN;
	const uint8_t *pSrc;

	if(!lzHave(pDec, 1)) {
		pDec->ubState = LZ_ST_ERR;
		return ulOut;
	}
	ulAvail = pDec->ulInFill - pDec->ulInPos;
	ulN = pDec->ulLitLeft;
	if(ulN > ulAvail) {
		ulN = ulAvail;
	}
	if(ulN > ulWant) {
		ulN = ulWant;
	}
	pSrc = pDec->pIn + pDec->ulInPos;
	lzCopy(pDst + ulOut, pSrc, ulN);
	lzWindowWrite(pDec, pSrc, ulN);
	pDec->ulInPos += ulN;
	pDec->ulLitLeft -= ulN;
	if(!pDec->ulLitLeft) {
		if(pDec->ulDecoded == pDec->ulDstSize) {
			pDec->ubState = LZ_ST_DONE;
		}
		else {
			pDec->ubState = LZ_ST_MATCH_HDR;
		}
	}
	return ulOut + ulN;
}

static uint32_t lzEmitMatch(
	tLzDec *pDec, uint8_t *pDst, uint32_t ulOut, uint32_t ulWant
) {
	uint32_t ulN = pDec->ulMatchLeft;
	uint8_t *pOut;
	uint32_t ulWinSrc;
	if(ulN > ulWant) {
		ulN = ulWant;
	}
	pOut = pDst + ulOut;
	ulWinSrc = (pDec->ulDecoded - pDec->ulMatchOff) & (LZ_WINDOW - 1);

	/* Disjoint lookback: copy then commit into the ring. Overlap must
	 * emit one byte at a time so later reads see bytes just written. */
	if(pDec->ulMatchOff >= ulN && ulWinSrc + ulN <= LZ_WINDOW) {
		lzCopy(pOut, pDec->pWindow + ulWinSrc, ulN);
		lzWindowWrite(pDec, pOut, ulN);
	}
	else if(pDec->ulMatchOff == 1) {
		uint8_t ub = pDec->pWindow[
			(pDec->ulDecoded - 1) & (LZ_WINDOW - 1)
		];
		lzFill(pOut, ub, ulN);
		lzWindowWrite(pDec, pOut, ulN);
	}
	else {
		uint32_t i;
		for(i = 0; i < ulN; ++i) {
			uint8_t ub = pDec->pWindow[
				(pDec->ulDecoded - pDec->ulMatchOff) & (LZ_WINDOW - 1)
			];
			pOut[i] = ub;
			pDec->pWindow[pDec->ulDecoded & (LZ_WINDOW - 1)] = ub;
			pDec->ulDecoded++;
		}
	}
	pDec->ulMatchLeft -= ulN;
	if(!pDec->ulMatchLeft) {
		pDec->ubState = LZ_ST_TOKEN;
	}
	return ulOut + ulN;
}

void lzDecInit(
	tLzDec *pDec,
	lzReadFn cbRead, void *pReadUser,
	uint8_t *pIn, uint32_t ulInCap,
	uint8_t *pWindow, uint32_t ulDstSize
) {
	pDec->cbRead = cbRead;
	pDec->pReadUser = pReadUser;
	pDec->pIn = pIn;
	pDec->ulInCap = ulInCap;
	pDec->pWindow = pWindow;
	pDec->ulDstSize = ulDstSize;
	lzDecReset(pDec);
}

void lzDecReset(tLzDec *pDec) {
	pDec->ulInPos = 0;
	pDec->ulInFill = 0;
	pDec->ulDecoded = 0;
	pDec->ulLitLeft = 0;
	pDec->ulMatchLeft = 0;
	pDec->ulMatchOff = 0;
	pDec->ubMatchNib = 0;
	pDec->ubState = LZ_ST_TOKEN;
}

uint32_t lzDecRead(tLzDec *pDec, uint8_t *pDst, uint32_t ulSize) {
	uint32_t ulOut = 0;

	if(!pDec || !pDst || pDec->ubState == LZ_ST_ERR) {
		return 0;
	}
	if(pDec->ubState == LZ_ST_DONE || !ulSize) {
		return 0;
	}

	while(ulOut < ulSize && pDec->ulDecoded < pDec->ulDstSize) {
		if(pDec->ubState == LZ_ST_LIT) {
			ulOut = lzEmitLit(pDec, pDst, ulOut, ulSize - ulOut);
			if(pDec->ubState == LZ_ST_ERR || pDec->ubState == LZ_ST_DONE) {
				break;
			}
			continue;
		}

		if(pDec->ubState == LZ_ST_MATCH) {
			ulOut = lzEmitMatch(pDec, pDst, ulOut, ulSize - ulOut);
			continue;
		}

		if(pDec->ubState == LZ_ST_MATCH_HDR) {
			uint32_t ulOff, ulMatch;
			if(!lzHave(pDec, 2)) {
				pDec->ubState = LZ_ST_ERR;
				break;
			}
			ulOff = ((uint32_t)lzGet(pDec) << 8) | lzGet(pDec);
			if(
				ulOff == 0 || ulOff > LZ_MAX_OFFSET ||
				ulOff > pDec->ulDecoded
			) {
				pDec->ubState = LZ_ST_ERR;
				break;
			}
			pDec->ulMatchOff = ulOff;
			ulMatch = (uint32_t)pDec->ubMatchNib + LZ_MIN_MATCH;
			if(pDec->ubMatchNib == 15) {
				if(!lzReadExtra(pDec, &ulMatch)) {
					break;
				}
			}
			if(ulMatch > pDec->ulDstSize - pDec->ulDecoded) {
				pDec->ubState = LZ_ST_ERR;
				break;
			}
			pDec->ulMatchLeft = ulMatch;
			pDec->ubState = LZ_ST_MATCH;
			continue;
		}

		if(pDec->ubState == LZ_ST_TOKEN) {
			uint8_t ubToken;
			uint32_t ulLit;
			if(!lzHave(pDec, 1)) {
				pDec->ubState = LZ_ST_ERR;
				break;
			}
			ubToken = lzGet(pDec);
			ulLit = ubToken >> 4;
			pDec->ubMatchNib = ubToken & 15;
			if(ulLit == 15) {
				if(!lzReadExtra(pDec, &ulLit)) {
					break;
				}
			}
			if(ulLit > pDec->ulDstSize - pDec->ulDecoded) {
				pDec->ubState = LZ_ST_ERR;
				break;
			}
			pDec->ulLitLeft = ulLit;
			if(ulLit) {
				pDec->ubState = LZ_ST_LIT;
			}
			else if(pDec->ulDecoded == pDec->ulDstSize) {
				pDec->ubState = LZ_ST_DONE;
				break;
			}
			else {
				pDec->ubState = LZ_ST_MATCH_HDR;
			}
			continue;
		}

		break;
	}

	return ulOut;
}
