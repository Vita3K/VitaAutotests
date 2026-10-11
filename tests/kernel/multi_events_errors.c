// sceKernelWaitMultipleEvents with lists and modes it should reject, and with events it can't use
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID set = sceKernelCreateSimpleEvent("set", 0, EVENT_BIT_A, NULL);
    const SceUID clear = sceKernelCreateSimpleEvent("clear", 0, 0, NULL);
    const SceUID deleted = sceKernelCreateSimpleEvent("deleted", 0, 0, NULL);
    sceKernelDeleteSimpleEvent(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    ResultEvent results[4];
    SceUInt32 timeout;

    // Events it can't use
    const SceUID bad[] = { 0x12345, deleted, sema, 0 };
    const char *const names[] = { "an unknown UID", "a deleted event", "a semaphore", "UID 0" };
    for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        WaitEvent events[2] = { { set, EVENT_BIT_A }, { bad[i], EVENT_BIT_A } };
        sceClibMemset(results, 0, sizeof(results));
        timeout = 1000;
        PRINTF(sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_OR, results, &timeout), "wait OR with a set event and %s", names[i]);
        PRINTF(results[0].eventId == set, "with %s: the first result is the set event", names[i]);
        PRINTF(results[1].result, "with %s: the second result", names[i]);
    }

    // Modes
    WaitEvent events[2] = { { set, EVENT_BIT_A }, { clear, EVENT_BIT_A } };
    static const SceUInt32 modes[] = { 0, 3, 4, 0x100 };
    for (unsigned int i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        timeout = 1000;
        PRINTF(sceKernelWaitMultipleEvents(events, 2, modes[i], results, &timeout), "wait with mode 0x%X", modes[i]);
    }

    // Lists
    timeout = 1000;
    PRINT("wait on 0 events", sceKernelWaitMultipleEvents(events, 0, EVENT_WAIT_MODE_OR, results, &timeout));
    WaitEvent zero_pattern[1] = { { set, 0 } };
    timeout = 1000;
    PRINT("wait for pattern 0", sceKernelWaitMultipleEvents(zero_pattern, 1, EVENT_WAIT_MODE_OR, results, &timeout));
    WaitEvent same_twice[2] = { { set, EVENT_BIT_A }, { set, EVENT_BIT_A } };
    timeout = 1000;
    PRINT("wait on the same event twice", sceKernelWaitMultipleEvents(same_twice, 2, EVENT_WAIT_MODE_AND, results, &timeout));
    timeout = 0;
    PRINT("wait AND with a timeout of 0", sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_AND, results, &timeout));
    timeout = 1000;
    PRINT("wait with callbacks", sceKernelWaitMultipleEventsCB(events, 2, EVENT_WAIT_MODE_OR, results, &timeout));
    static WaitEvent many[65];
    static ResultEvent many_results[65];
    for (unsigned int i = 0; i < 65; i++)
        many[i] = (WaitEvent){ set, EVENT_BIT_A };
    timeout = 1000;
    PRINT("wait on 64 events", sceKernelWaitMultipleEvents(many, 64, EVENT_WAIT_MODE_OR, many_results, &timeout));
    timeout = 1000;
    PRINT("wait on 65 events", sceKernelWaitMultipleEvents(many, 65, EVENT_WAIT_MODE_OR, many_results, &timeout));

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    timeout = 1000;
    PRINT("wait without a result list", sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_OR, NULL, &timeout));
    timeout = 1000;
    PRINT("wait on a NULL list", sceKernelWaitMultipleEvents(NULL, 2, EVENT_WAIT_MODE_OR, results, &timeout));

    test_exit();
    return 0;
}
