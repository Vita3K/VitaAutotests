// Every event flag call with a UID that isn't an event flag it can use, and with arguments it should
// reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID flag;
static int wait_result;

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    unsigned int bits;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitEventFlag(flag, 0x8, EVF_WAITMODE_OR, &bits, &timeout);
    return 0;
}

int main(void) {
    const SceUID deleted = sceKernelCreateEventFlag("deleted", 0, 0, NULL);
    sceKernelDeleteEventFlag(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted event flag", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt timeout = 1000;
        unsigned int bits = 0xDEAD;
        SceUInt32 waiting = 0xDEAD;
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelSetEventFlag(ids[i], 1), "set %s", names[i]);
        PRINTF(sceKernelClearEventFlag(ids[i], 0), "clear %s", names[i]);
        PRINTF(sceKernelWaitEventFlag(ids[i], 1, EVF_WAITMODE_OR, &bits, &timeout), "wait on %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelWaitEventFlagCB(ids[i], 1, EVF_WAITMODE_OR, &bits, &timeout), "wait with callbacks on %s", names[i]);
        PRINTF(sceKernelPollEventFlag(ids[i], 1, EVF_WAITMODE_OR, &bits), "poll %s", names[i]);
        PRINTF(sceKernelCancelEventFlag(ids[i], 0, &waiting), "cancel %s", names[i]);
        PRINTF(sceKernelGetEventFlagInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseEventFlag(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteEventFlag(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the event flag calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateEventFlag("", 0, 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateEventFlag("f", 0x80000000, 0, NULL)));
    PRINT("create with every bit set", test_created(sceKernelCreateEventFlag("f", 0, 0xFFFFFFFF, NULL)));

    flag = sceKernelCreateEventFlag("flag", 0, 0x3, NULL);
    unsigned int bits = 0xDEAD;
    SceUInt timeout = 1000;

    // Wait modes on a flag with bits 0x3
    PRINT("wait with both clear modes", sceKernelWaitEventFlag(flag, 0x1, EVF_WAITMODE_OR | EVF_WAITMODE_CLEAR_ALL | EVF_WAITMODE_CLEAR_PAT, &bits, &timeout));
    timeout = 1000;
    PRINT("wait with mode 0x8", sceKernelWaitEventFlag(flag, 0x1, 0x8, &bits, &timeout));
    timeout = 0;
    PRINT("wait with a timeout of 0 for a missing bit", sceKernelWaitEventFlag(flag, 0x4, EVF_WAITMODE_OR, &bits, &timeout));
    PRINT("poll AND for one set and one missing bit", sceKernelPollEventFlag(flag, 0x5, EVF_WAITMODE_AND, &bits));
    PRINT("the bits it reports", bits);
    PRINT("set 0", sceKernelSetEventFlag(flag, 0));
    PRINT("clear with every bit set", sceKernelClearEventFlag(flag, 0xFFFFFFFF));
    PRINT("poll after it", sceKernelPollEventFlag(flag, 0x3, EVF_WAITMODE_AND, &bits));

    // A second waiter on a flag without the multi attribute
    const SceUID waiter = test_start_thread(wait_thread);
    test_wait_until_waiting(waiter);
    timeout = 1000;
    PRINT("a second waiter without the multi attribute", sceKernelWaitEventFlag(flag, 0x8, EVF_WAITMODE_OR, &bits, &timeout));
    SceUInt32 waiting = 0xDEAD;
    PRINT("cancel with a waiter", sceKernelCancelEventFlag(flag, 0x10, &waiting));
    test_join_thread(waiter);
    PRINT("the waiting count", waiting);
    PRINT("the waiter's result", wait_result);
    bits = 0xDEAD;
    sceKernelPollEventFlag(flag, 0xFFFFFFFF, EVF_WAITMODE_OR, &bits);
    PRINT("the bits the cancel set", bits);
    PRINT("cancel without a waiting count pointer", sceKernelCancelEventFlag(flag, 0, NULL));

    // Info
    SceKernelEventFlagInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetEventFlagInfo(flag, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelEventFlagInfo info;
        unsigned char bytes[sizeof(SceKernelEventFlagInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelEventFlagInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetEventFlagInfo(flag, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelEventFlagInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    sceKernelSetEventFlag(flag, 0x1);
    PRINT("poll without a bits pointer", sceKernelPollEventFlag(flag, 0x1, EVF_WAITMODE_OR, NULL));
    timeout = 1000;
    PRINT("wait without a bits pointer", sceKernelWaitEventFlag(flag, 0x1, EVF_WAITMODE_OR, NULL, &timeout));
    PRINT("info with a NULL pointer", sceKernelGetEventFlagInfo(flag, NULL));
    PRINT("open with a NULL name", sceKernelOpenEventFlag(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateEventFlag(NULL, 0, 0, NULL)));

    test_exit();
    return 0;
}
