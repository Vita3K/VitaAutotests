// Simple event calls with NULL pointers on one event, the pattern first and then polls and waits
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID event = sceKernelCreateSimpleEvent("event", 0, EVENT_BIT_A, NULL);
    PRINT("get the pattern into a NULL pointer", sceKernelGetEventPattern(event, NULL));
    for (int i = 0; i < 10; i++) {
        SceUInt32 bits = 0;
        SceUInt64 user_data = 0;
        PRINTF(sceKernelPollEvent(event, EVENT_BIT_A, &bits, &user_data), "poll %d", i);
    }
    for (int i = 0; i < 10; i++)
        PRINTF(sceKernelPollEvent(event, EVENT_BIT_A, NULL, NULL), "poll without pointers %d", i);
    for (int i = 0; i < 10; i++) {
        SceUInt32 timeout = 1000;
        PRINTF(sceKernelWaitEvent(event, EVENT_BIT_A, NULL, NULL, &timeout), "wait without pointers %d", i);
    }
    test_exit();
    return 0;
}
