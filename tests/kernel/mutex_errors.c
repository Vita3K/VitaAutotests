// Every mutex call with a UID that isn't a mutex it can use, and with arguments it should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID mutex, release;

// Locks the mutex and keeps it until release is signaled
static int owner_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, NULL);
    sceKernelWaitSema(release, 1, &timeout);
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

int main(void) {
    const SceUID deleted = sceKernelCreateMutex("deleted", 0, 0, NULL);
    sceKernelDeleteMutex(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted mutex", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        SceUInt32 timeout = 1000;
        int waiting = 0xDEAD;
        MutexInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelLockMutex(ids[i], 1, &timeout), "lock %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelLockMutexCB(ids[i], 1, &timeout), "lock with callbacks %s", names[i]);
        PRINTF(sceKernelTryLockMutex(ids[i], 1), "try lock %s", names[i]);
        PRINTF(sceKernelUnlockMutex(ids[i], 1), "unlock %s", names[i]);
        PRINTF(sceKernelCancelMutex(ids[i], 0, &waiting), "cancel %s", names[i]);
        PRINTF(sceKernelGetMutexInfo(ids[i], (SceKernelMutexInfo *)&info), "info of %s", names[i]);
        PRINTF(sceKernelCloseMutex(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteMutex(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the mutex calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateMutex("", 0, 0, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateMutex("m", 0x80000000, 0, NULL)));
    PRINT("create recursive with the largest count", test_created(sceKernelCreateMutex("m", MUTEX_ATTR_RECURSIVE, 0x7FFFFFFF, NULL)));

    // Counts on a recursive mutex this thread holds once
    mutex = sceKernelCreateMutex("mutex", MUTEX_ATTR_RECURSIVE, 1, NULL);
    PRINT("unlock 0", sceKernelUnlockMutex(mutex, 0));
    PRINT("unlock -1", sceKernelUnlockMutex(mutex, -1));
    PRINT("unlock 2 of 1", sceKernelUnlockMutex(mutex, 2));
    PRINT("lock up to the largest count", sceKernelLockMutex(mutex, 0x7FFFFFFE, NULL));
    PRINT("lock past the largest count", sceKernelTryLockMutex(mutex, 1));
    PRINT("unlock all of it", sceKernelUnlockMutex(mutex, 0x7FFFFFFF));
    PRINT("unlock once more", sceKernelUnlockMutex(mutex, 1));

    // Cancel
    int waiting = 0xDEAD;
    PRINT("cancel an unlocked mutex to 0", sceKernelCancelMutex(mutex, 0, &waiting));
    PRINT("its waiting count", waiting);
    PRINT("cancel to 2", sceKernelCancelMutex(mutex, 2, NULL));
    PRINT("try lock after the cancel to 2", sceKernelTryLockMutex(mutex, 1));
    PRINT("unlock after it", sceKernelUnlockMutex(mutex, 1));
    PRINT("cancel to -1", sceKernelCancelMutex(mutex, -1, NULL));
    PRINT("cancel to -2", sceKernelCancelMutex(mutex, -2, NULL));
    sceKernelCancelMutex(mutex, 0, NULL);
    const SceUID plain = sceKernelCreateMutex("plain", 0, 0, NULL);
    PRINT("cancel a non-recursive mutex to 2", sceKernelCancelMutex(plain, 2, NULL));

    // Another thread holds it
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    sceKernelUnlockMutex(mutex, 0x7FFFFFFF);
    const SceUID owner = test_start_thread(owner_thread);
    test_wait_until_waiting(owner);
    PRINT("try lock a mutex another thread holds", sceKernelTryLockMutex(mutex, 1));
    PRINT("unlock it", sceKernelUnlockMutex(mutex, 1));
    PRINT("cancel it", sceKernelCancelMutex(mutex, 0, NULL));
    PRINT("delete it while held", sceKernelDeleteMutex(mutex));
    sceKernelSignalSema(release, 1);
    test_join_thread(owner);

    // Info
    MutexInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetMutexInfo(plain, (SceKernelMutexInfo *)&info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        MutexInfo info;
        unsigned char bytes[sizeof(MutexInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(MutexInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetMutexInfo(plain, (SceKernelMutexInfo *)&big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(MutexInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("info with a NULL pointer", sceKernelGetMutexInfo(plain, NULL));
    PRINT("open with a NULL name", sceKernelOpenMutex(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateMutex(NULL, 0, 0, NULL)));

    test_exit();
    return 0;
}
