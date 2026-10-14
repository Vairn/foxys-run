#include "debug/stack.h"

#if !defined(ACE_HOST)

#include <proto/exec.h>
#include <exec/memory.h>

/* Boot CLI stack is ~4000 bytes. ACE_DEBUG's first logWrite() runs before
 * LoadView(0), so an overflow gurus with the CLI still on screen.
 * 32 KB matches a comfortable Amiga process stack and stays out of chip
 * RAM when trapdoor memory is present. */
#define SUNNY_STACK_BYTES (32UL * 1024)

/* Args on entry: 4(sp) = fn, 8(sp) = initial SP.
 * Caller has already Disable()'d and published the new stack bounds.
 * Enable/Disable trash a0-a1/d0-d1, so the function pointer stays in a2.
 * Returns with interrupts disabled again so the caller can restore bounds. */
__asm__(
	"	.globl sunnySwitchStack\n"
	"	.type sunnySwitchStack, @function\n"
	"sunnySwitchStack:\n"
	"	move.l %a2,-(%sp)\n"
	"	move.l %a6,-(%sp)\n"
	"	move.l 12(%sp),%a2\n"
	"	move.l 16(%sp),%a1\n"
	"	move.l %sp,%d0\n"
	"	move.l %a1,%sp\n"
	"	move.l %d0,-(%sp)\n"
	"	move.l 4.w,%a6\n"
	"	jsr -126(%a6)\n"
	"	jsr (%a2)\n"
	"	move.l 4.w,%a6\n"
	"	jsr -120(%a6)\n"
	"	move.l (%sp)+,%sp\n"
	"	move.l (%sp)+,%a6\n"
	"	move.l (%sp)+,%a2\n"
	"	rts\n"
	"	.size sunnySwitchStack, .-sunnySwitchStack\n"
);

void sunnySwitchStack(void (*fn)(void), void *pUpper);

static void *allocStack(ULONG ulSize) {
	void *pStack = AllocMem(ulSize, MEMF_FAST | MEMF_CLEAR);
	if(pStack) {
		return pStack;
	}

	/* A500 trapdoor RAM is public but not FAST. Hold the chip pool so
	 * the stack lands in slow RAM, then give chip back to the playfield. */
	Forbid();
	ULONG ulHold = AvailMem(MEMF_CHIP | MEMF_LARGEST);
	void *pHold = 0;
	if(ulHold > 64) {
		ulHold -= 32;
		pHold = AllocMem(ulHold, MEMF_CHIP);
	}
	pStack = AllocMem(ulSize, MEMF_PUBLIC | MEMF_CLEAR);
	if(!pStack && pHold) {
		FreeMem(pHold, ulHold);
		pHold = 0;
		pStack = AllocMem(ulSize, MEMF_PUBLIC | MEMF_CLEAR);
	}
	else if(pHold) {
		FreeMem(pHold, ulHold);
	}
	Permit();
	return pStack;
}

void sunnyGrowStack(void (*fn)(void)) {
	SysBase = *(struct ExecBase **)4;

	struct Task *pTask = FindTask(NULL);
	ULONG ulHave = (ULONG)pTask->tc_SPUpper - (ULONG)pTask->tc_SPLower;
	if(ulHave >= SUNNY_STACK_BYTES) {
		fn();
		return;
	}

	UBYTE *pStack = allocStack(SUNNY_STACK_BYTES);
	if(!pStack) {
		fn();
		return;
	}

	APTR pOldLower = pTask->tc_SPLower;
	APTR pOldUpper = pTask->tc_SPUpper;
	UBYTE *pUpper = pStack + SUNNY_STACK_BYTES;

	Disable();
	pTask->tc_SPLower = pStack;
	pTask->tc_SPUpper = pUpper;
	sunnySwitchStack(fn, pUpper);
	pTask->tc_SPLower = pOldLower;
	pTask->tc_SPUpper = pOldUpper;
	Enable();

	FreeMem(pStack, SUNNY_STACK_BYTES);
}

#else

void sunnyGrowStack(void (*fn)(void)) {
	fn();
}

#endif
