// Polls of an auto reset timer whose repeating event fires several times between them. The timer
// has an auto reset set event of 500 ms that repeats, and it starts. The main thread sleeps 1.25 s,
// after two fires, then polls for the timer bit 0x8000, polls again at once, sleeps 500 ms more,
// past the next fire, and polls a last time. Every poll is 250 ms from a fire. The events that fired before the first poll count as one, since the bit is set once and the
// poll that takes it clears it. The test prints each poll's result and the bits it got.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD

// Polls the timer for its bit, and prints the result and the bits the poll got
static void print_poll(const char *name, SceUID timer) {
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = 0;
    const int polled = sceKernelPollEvent(timer, EVENT_TIMER, &bits, &user_data);
    PRINTF(polled, "%s", name);
    PRINTF(bits, "%s: the bits it got", name);
}

int main(void) {
    SceUInt64 interval = 500000;
    const SceUID timer = sceKernelCreateTimer("missed", EVENT_ATTR_AUTO_RESET, NULL);
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 1);
    sceKernelStartTimer(timer);

    sceKernelDelayThread(1250000);
    print_poll("the first poll", timer);
    print_poll("the second poll", timer);
    sceKernelDelayThread(500000);
    print_poll("the third poll", timer);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    test_exit();
    return 0;
}
