// Every simple event call with a UID that isn't a simple event it can use, and with arguments it
// should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID deleted = sceKernelCreateSimpleEvent("deleted", 0, 0, NULL);
    sceKernelDeleteSimpleEvent(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted simple event", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt32 timeout = 1000;
        SceUInt32 bits = 0xDEAD;
        SceUInt64 user_data = 0;
        SceUInt32 waiting = 0xDEAD;
        EventInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelSetEvent(ids[i], EVENT_BIT_A, 0), "set %s", names[i]);
        PRINTF(sceKernelPulseEvent(ids[i], EVENT_BIT_A, 0), "pulse %s", names[i]);
        PRINTF(sceKernelClearEvent(ids[i], 0), "clear %s", names[i]);
        PRINTF(sceKernelGetEventPattern(ids[i], &bits), "get the pattern of %s", names[i]);
        PRINTF(sceKernelPollEvent(ids[i], EVENT_BIT_A, &bits, &user_data), "poll %s", names[i]);
        PRINTF(sceKernelWaitEvent(ids[i], EVENT_BIT_A, &bits, &user_data, &timeout), "wait on %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelWaitEventCB(ids[i], EVENT_BIT_A, &bits, &user_data, &timeout), "wait with callbacks on %s", names[i]);
        PRINTF(sceKernelCancelEvent(ids[i], &waiting), "cancel %s", names[i]);
        PRINTF(sceKernelGetEventInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseSimpleEvent(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteSimpleEvent(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the simple event calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateSimpleEvent("", 0, 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateSimpleEvent("e", 0x80000000, 0, NULL)));
    PRINT("create with the openable attribute", test_created(sceKernelCreateSimpleEvent("e", ATTR_OPENABLE, 0, NULL)));
    PRINT("create with the priority attribute", test_created(sceKernelCreateSimpleEvent("e", ATTR_TH_PRIO, 0, NULL)));
    PRINT("create with the multi attribute", test_created(sceKernelCreateSimpleEvent("e", EVF_ATTR_MULTI, 0, NULL)));

    test_exit();
    return 0;
}
