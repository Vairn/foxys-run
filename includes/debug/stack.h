#ifndef SUNNY_STACK_H
#define SUNNY_STACK_H

/**
 * Run fn on a stack large enough for ACE_DEBUG.
 * Kickstart 1.3's boot CLI stack is about 4 KB, and StackSwap() is Kickstart 2.0.
 */
void sunnyGrowStack(void (*fn)(void));

#endif
