// Event flag and simple event edge cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

SceInt32 sceKernelCancelEvent(SceUID id, SceUInt32 *num_wait_threads);

int main(void) {
    unsigned int bits;
    SceUInt timeout;
    SceUInt32 result, pattern;
    SceUInt64 user_data;

    // Event flags: pattern 0 and wait modes that aren't documented
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    PRINT("poll for pattern 0", sceKernelPollEventFlag(flag, 0, EVF_WAITMODE_OR, &bits));
    timeout = 1000;
    PRINT("wait for pattern 0", sceKernelWaitEventFlag(flag, 0, EVF_WAITMODE_OR, &bits, &timeout));
    PRINT("poll with both clear modes", sceKernelPollEventFlag(flag, 0x1, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL | EVF_WAITMODE_CLEAR_PAT, &bits));
    sceKernelSetEventFlag(flag, 0x1);
    PRINT("poll with mode 0x8", sceKernelPollEventFlag(flag, 0x1, 0x8, &bits));
    PRINT("poll with mode 0x100", sceKernelPollEventFlag(flag, 0x1, 0x100, &bits));
    PRINT("poll without a result pointer", sceKernelPollEventFlag(flag, 0x1, EVF_WAITMODE_OR, NULL));
    timeout = 0;
    PRINT("wait with a timeout of 0 that isn't met", sceKernelWaitEventFlag(flag, 0x2, EVF_WAITMODE_AND, &bits, &timeout));
    SceUInt32 waiting = 0xDEAD;
    PRINT("cancel with no waiters", sceKernelCancelEventFlag(flag, 0x6, &waiting));
    PRINT("it counts no waiters", waiting);
    PRINT("the pattern after the cancel", sceKernelPollEventFlag(flag, 0xFFFFFFFF, EVF_WAITMODE_OR, &bits) == 0 ? bits : 0);
    PRINT("create with attribute 0xFFFF", test_created(sceKernelCreateEventFlag("attrs", 0xFFFF, 0, NULL)));

    // Simple events: pattern 0 and the high bits
    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    PRINT("set pattern 0", sceKernelSetEvent(event, 0, 0));
    PRINT("poll for pattern 0", sceKernelPollEvent(event, 0, &result, &user_data));
    PRINT("set every bit", sceKernelSetEvent(event, 0xFFFFFFFF, 0));
    pattern = 0xDEAD;
    sceKernelGetEventPattern(event, &pattern);
    PRINT("the bits after setting every bit", pattern);
    PRINT("clear every bit", sceKernelClearEvent(event, 0));
    PRINT("set bit 31", sceKernelSetEvent(event, 0x80000000, 0));
    PRINT("pulse bit 31", sceKernelPulseEvent(event, 0x80000000, 0));
    SceUInt32 timeout32 = 0;
    PRINT("wait with a timeout of 0 that isn't met", sceKernelWaitEvent(event, 0x1, &result, &user_data, &timeout32));
    waiting = 0xDEAD;
    PRINT("cancel with no waiters", sceKernelCancelEvent(event, &waiting));
    PRINT("it counts no waiters", waiting);
    PRINT("create with attribute 0xFFFF", test_created(sceKernelCreateSimpleEvent("attrs", 0xFFFF, 0, NULL)));
    PRINT("create with every bit set", test_created(sceKernelCreateSimpleEvent("full", 0, 0xFFFFFFFF, NULL)));

    test_exit();
    return 0;
}
