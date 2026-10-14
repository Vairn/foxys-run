#ifndef _ACE_UTILS_LZ_H_
#define _ACE_UTILS_LZ_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LZ_MIN_MATCH 3
#define LZ_MAX_OFFSET 4095
#define LZ_WINDOW 4096
#define LZ_IN_BUF 512

/**
 * LZ4-style block codec, 12-bit lookback (4K window), big-endian offsets.
 *
 * Stream is a sequence of:
 *   token: (literal_length << 4) | (match_length - 3)
 *   extra literal length bytes if nibble == 15 (255, 255, ..., rest)
 *   literal bytes
 *   unless this is the last sequence:
 *     offset: UWORD big-endian, 1..4095, distance back in the output
 *     extra match length bytes if nibble == 15
 *     copy match_length bytes from dst[-offset] (overlap-safe)
 *
 * The last sequence is literals only (match nibble is zero, no offset).
 *
 * lzUnpack() is the fast path: one shot into a caller buffer. pak open uses
 * it, then serves reads from FAST. lzDecRead() is the streaming fallback.
 *
 * @return Bytes written (must equal ulDstSize) or 0 on error.
 */
uint32_t lzUnpack(
	const uint8_t *pSrc, uint32_t ulSrcSize,
	uint8_t *pDst, uint32_t ulDstSize
);

typedef uint32_t (*lzReadFn)(void *pUser, uint8_t *pDest, uint32_t ulSize);

enum {
	LZ_ST_TOKEN = 0,
	LZ_ST_LIT,
	LZ_ST_MATCH_HDR,
	LZ_ST_MATCH,
	LZ_ST_DONE,
	LZ_ST_ERR
};

typedef struct tLzDec {
	lzReadFn cbRead;
	void *pReadUser;
	uint8_t *pIn;
	uint32_t ulInCap;
	uint32_t ulInPos;
	uint32_t ulInFill;
	uint8_t *pWindow;
	uint32_t ulDecoded;
	uint32_t ulDstSize;
	uint32_t ulLitLeft;
	uint32_t ulMatchLeft;
	uint32_t ulMatchOff;
	uint8_t ubMatchNib;
	uint8_t ubState;
} tLzDec;

void lzDecInit(
	tLzDec *pDec,
	lzReadFn cbRead, void *pReadUser,
	uint8_t *pIn, uint32_t ulInCap,
	uint8_t *pWindow, uint32_t ulDstSize
);

void lzDecReset(tLzDec *pDec);

uint32_t lzDecRead(tLzDec *pDec, uint8_t *pDst, uint32_t ulSize);

#ifdef __cplusplus
}
#endif

#endif
