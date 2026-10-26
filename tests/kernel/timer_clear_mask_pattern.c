// Clearing the bits of a timer's event pattern with masks. A timer with a one shot set event of 100
// ms starts, and the main thread waits for the timer bit 0x8000 with a timeout of 2 s, so the event
// has fired by then. The pattern is read, then cleared three times, with ~0x8000, with 0x10000 and
// with 0. The test prints the wait's result, and after each step the result of the call and the
// pattern. The pattern is printed as comparisons for the timer bit, the create bit 0x4 and any
// other system bit, and as the user bits, since the timer holds system bits of its own.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The create bit a new timer or event starts with
#define CREATE_BIT 0x4
// The user bits of an event, the 0xFF000000 range that a game may set
#define USER_BITS 0xFF000000

static SceUID timer;

static SceUInt32 pattern_of(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info.eventPattern;
}

static void print_pattern(const char *name, SceUInt32 pattern) {
    PRINTF((pattern & EVENT_TIMER) != 0, "%s: the timer bit is set", name);
    PRINTF((pattern & CREATE_BIT) != 0, "%s: the create bit is set", name);
    PRINTF(pattern & USER_BITS, "%s: the user bits", name);
    PRINTF((pattern & ~(SceUInt32)(EVENT_TIMER | CREATE_BIT | USER_BITS)) != 0,
           "%s: another system bit is set", name);
}

int main(void) {
    SceUInt64 interval = 100000;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    SceUInt32 timeout = TEST_WAIT_US;

    timer = sceKernelCreateTimer("clear", 0, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(timer);
    PRINT("the wait for the fired event",
          sceKernelWaitEvent(timer, EVENT_TIMER, &bits, &user_data, &timeout));
    print_pattern("the pattern after the event fired", pattern_of(timer));

    PRINT("clear with ~0x8000", sceKernelClearEvent(timer, ~(SceUInt32)EVENT_TIMER));
    print_pattern("the pattern after clearing with ~0x8000", pattern_of(timer));

    PRINT("clear with 0x10000", sceKernelClearEvent(timer, 0x10000));
    print_pattern("the pattern after clearing with 0x10000", pattern_of(timer));

    PRINT("clear with 0", sceKernelClearEvent(timer, 0));
    print_pattern("the pattern after clearing with 0", pattern_of(timer));

    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
