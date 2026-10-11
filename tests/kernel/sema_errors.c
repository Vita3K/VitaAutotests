// Every semaphore call with a UID that isn't a semaphore it can use, and with arguments it should
// reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID deleted = sceKernelCreateSema("deleted", 0, 0, 1, NULL);
    sceKernelDeleteSema(deleted);
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID ids[] = { 0x12345, deleted, mutex, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted semaphore", "a mutex", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt32 timeout = 1000;
        int waiting = 0xDEAD;
        SceKernelSemaInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelSignalSema(ids[i], 1), "signal %s", names[i]);
        PRINTF(sceKernelWaitSema(ids[i], 1, &timeout), "wait on %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelWaitSemaCB(ids[i], 1, &timeout), "wait with callbacks on %s", names[i]);
        PRINTF(sceKernelPollSema(ids[i], 1), "poll %s", names[i]);
        PRINTF(sceKernelCancelSema(ids[i], 0, &waiting), "cancel %s", names[i]);
        PRINTF(sceKernelGetSemaInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseSema(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteSema(ids[i]), "delete %s", names[i]);
    }
    PRINT("the mutex survives the semaphore calls", sceKernelTryLockMutex(mutex, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateSema("", 0, 0, 1, NULL)));
    PRINT("create with a count equal to the maximum", test_created(sceKernelCreateSema("s", 0, 3, 3, NULL)));
    PRINT("create with the largest maximum", test_created(sceKernelCreateSema("s", 0, 0, 0x7FFFFFFF, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateSema("s", 0x80000000, 0, 1, NULL)));

    // Counts on a semaphore with a count of 1 and a maximum of 2
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 2, NULL);
    PRINT("signal up to the maximum", sceKernelSignalSema(sema, 1));
    PRINT("signal past the maximum", sceKernelSignalSema(sema, 1));
    PRINT("signal 0", sceKernelSignalSema(sema, 0));
    PRINT("signal a huge count", sceKernelSignalSema(sema, 0x7FFFFFFF));
    PRINT("poll for more than the maximum", sceKernelPollSema(sema, 3));
    PRINT("poll for all of it", sceKernelPollSema(sema, 2));
    PRINT("poll an empty semaphore", sceKernelPollSema(sema, 1));
    SceUInt32 timeout = 0;
    PRINT("wait with a timeout of 0 on an empty semaphore", sceKernelWaitSema(sema, 1, &timeout));
    timeout = 1000;
    PRINT("wait for 0 with a timeout", sceKernelWaitSema(sema, 0, &timeout));

    // Cancel
    int waiting = 0xDEAD;
    PRINT("cancel past the maximum", sceKernelCancelSema(sema, 3, &waiting));
    PRINT("its waiting count", waiting);
    waiting = 0xDEAD;
    PRINT("cancel to -1", sceKernelCancelSema(sema, -1, &waiting));
    PRINT("its waiting count", waiting);
    PRINT("cancel to -2", sceKernelCancelSema(sema, -2, NULL));
    PRINT("cancel without a waiting count pointer", sceKernelCancelSema(sema, 2, NULL));
    PRINT("poll after the cancel", sceKernelPollSema(sema, 2));

    // Info
    SceKernelSemaInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetSemaInfo(sema, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelSemaInfo info;
        unsigned char bytes[sizeof(SceKernelSemaInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelSemaInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetSemaInfo(sema, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelSemaInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("info with a NULL pointer", sceKernelGetSemaInfo(sema, NULL));
    PRINT("open with a NULL name", sceKernelOpenSema(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateSema(NULL, 0, 0, 1, NULL)));

    PRINT("delete it", sceKernelDeleteSema(sema));
    PRINT("delete it again", sceKernelDeleteSema(sema));

    test_exit();
    return 0;
}
