// Every condition variable call with a UID that isn't a condition variable it can use, and with
// arguments it should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID deleted = sceKernelCreateCond("deleted", 0, mutex, NULL);
    sceKernelDeleteCond(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted condition variable", "a semaphore", "UID 0", "UID -1" };

    sceKernelLockMutex(mutex, 1, NULL);
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt timeout = 1000;
        SceKernelCondInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelWaitCond(ids[i], &timeout), "wait on %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelWaitCondCB(ids[i], &timeout), "wait with callbacks on %s", names[i]);
        PRINTF(sceKernelSignalCond(ids[i]), "signal %s", names[i]);
        PRINTF(sceKernelSignalCondAll(ids[i]), "signal all of %s", names[i]);
        PRINTF(sceKernelSignalCondTo(ids[i], sceKernelGetThreadId()), "signal this thread on %s", names[i]);
        PRINTF(sceKernelGetCondInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseCond(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteCond(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the condition variable calls", sceKernelPollSema(sema, 1));
    PRINT("the mutex is still held", sceKernelUnlockMutex(mutex, 1));

    // Creation with a mutex it can't use
    const SceUID deleted_mutex = sceKernelCreateMutex("deleted", 0, 0, NULL);
    sceKernelDeleteMutex(deleted_mutex);
    PRINT("create with an unknown mutex", test_created(sceKernelCreateCond("c", 0, 0x12345, NULL)));
    PRINT("create with a deleted mutex", test_created(sceKernelCreateCond("c", 0, deleted_mutex, NULL)));
    PRINT("create with a semaphore as the mutex", test_created(sceKernelCreateCond("c", 0, sema, NULL)));
    PRINT("create with mutex 0", test_created(sceKernelCreateCond("c", 0, 0, NULL)));
    PRINT("create with an empty name", test_created(sceKernelCreateCond("", 0, mutex, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateCond("c", 0x80000000, mutex, NULL)));
    const SceUID recursive = sceKernelCreateMutex("recursive", MUTEX_ATTR_RECURSIVE, 0, NULL);
    PRINT("create with a recursive mutex", test_created(sceKernelCreateCond("c", 0, recursive, NULL)));

    // Signals with nobody waiting, to threads that can't be woken
    const SceUID cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    PRINT("signal to an unknown thread", sceKernelSignalCondTo(cond, 0x12345));
    PRINT("signal to thread 0", sceKernelSignalCondTo(cond, 0));
    PRINT("signal to a semaphore", sceKernelSignalCondTo(cond, sema));
    PRINT("signal all with nobody waiting", sceKernelSignalCondAll(cond));
    SceUInt timeout = 0;
    sceKernelLockMutex(mutex, 1, NULL);
    PRINT("wait with a timeout of 0", sceKernelWaitCond(cond, &timeout));
    PRINT("the mutex is held again after it", sceKernelUnlockMutex(mutex, 1));

    // Deleting the mutex first
    const SceUID orphan_mutex = sceKernelCreateMutex("orphan", 0, 0, NULL);
    const SceUID orphan = sceKernelCreateCond("orphan", 0, orphan_mutex, NULL);
    sceKernelDeleteMutex(orphan_mutex);
    PRINT("signal a condition variable whose mutex is deleted", sceKernelSignalCond(orphan));
    timeout = 1000;
    PRINT("wait on it", sceKernelWaitCond(orphan, &timeout));
    PRINT("delete it", sceKernelDeleteCond(orphan));

    // Info
    SceKernelCondInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetCondInfo(cond, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelCondInfo info;
        unsigned char bytes[sizeof(SceKernelCondInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelCondInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetCondInfo(cond, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelCondInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("info with a NULL pointer", sceKernelGetCondInfo(cond, NULL));
    PRINT("open with a NULL name", sceKernelOpenCond(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateCond(NULL, 0, mutex, NULL)));

    test_exit();
    return 0;
}
