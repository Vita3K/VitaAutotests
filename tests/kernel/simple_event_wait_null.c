// Waiting on a simple event without result pointers, in its own test since the wait may never return
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID event = sceKernelCreateSimpleEvent("event", 0, EVENT_BIT_A, NULL);
    SceUInt32 timeout = 1000;
    PRINT("wait for a set bit without result pointers", sceKernelWaitEvent(event, EVENT_BIT_A, NULL, NULL, &timeout));

    test_exit();
    return 0;
}
