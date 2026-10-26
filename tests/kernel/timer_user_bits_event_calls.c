// The event calls on a timer that isn't started, with a user bit, a pulse of another user bit, and
// the timer's own bit 0x8000. A thread waits for the user bit 0x01000000 with a 2 s timeout while
// the main thread sets it, then the main thread pulses 0x02000000 with no waiter, sets 0x8000, and
// clears the user bit with a mask that keeps all other bits. The model timer_wait_pattern covers
// which bits a wait and a poll of a timer get. The cases:
// - the set of the user bit with a waiter, its result, then the waiter's result, and whether the
//   bits it got are the user bit.
// - the pulse of 0x02000000 with no waiter, and its result.
// - the set of 0x8000, its result, and whether the pattern after it has 0x8000.
// - the clear of the user bit with its mask, its result, and the pattern after it.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID timer;
static volatile int waiter_result;
static volatile SceUInt32 waiter_bits;

static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0xDEAD;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitEvent(timer, EVENT_BIT_A, &bits, &user_data, &timeout);
    waiter_bits = bits;
    return 0;
}

// Reads the timer's pattern into pattern, and returns the call's result
static int read_pattern(SceUInt32 *pattern) {
    *pattern = 0;
    return sceKernelGetEventPattern(timer, pattern);
}

// Prints the pattern call's result under the label when, and the pattern if the call succeeded
static void print_pattern(const char *when) {
    SceUInt32 pattern = 0;
    const int result = read_pattern(&pattern);
    PRINTF(result, "%s: the pattern call", when);
    if (result == 0)
        PRINTF(pattern, "%s: the pattern", when);
}

int main(void) {
    timer = sceKernelCreateTimer("user bits", 0, NULL);
    waiter_result = 0xDEAD;
    waiter_bits = 0xDEAD;

    const SceUID thread = test_start_thread(waiter_entry);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_EVENT, timer));
    PRINT("set the user bit with a waiter", sceKernelSetEvent(timer, EVENT_BIT_A, 0));
    test_join_thread(thread);
    PRINT("the waiter's result", waiter_result);
    PRINT("the bits the waiter got are the user bit", waiter_bits == EVENT_BIT_A);
    print_pattern("after the set of the user bit");

    PRINT("pulse the user bit 0x02000000 with no waiter",
          sceKernelPulseEvent(timer, EVENT_BIT_B, 0));
    print_pattern("after the pulse");

    PRINT("set the timer bit 0x8000", sceKernelSetEvent(timer, EVENT_TIMER, 0));
    SceUInt32 pattern = 0;
    const int read = read_pattern(&pattern);
    PRINT("the pattern after the set has 0x8000", read == 0 && (pattern & EVENT_TIMER) != 0);
    print_pattern("after the set of 0x8000");

    PRINT("clear the user bit with a mask that keeps the others",
          sceKernelClearEvent(timer, ~EVENT_BIT_A));
    print_pattern("after the clear of the user bit");

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
