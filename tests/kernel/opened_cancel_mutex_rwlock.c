// Cancels a mutex and a reader/writer lock through an opened UID while a waiter queues through the
// creating UID, and prints what each cancel returns and writes, what the waiter gets, and the lock
// count and owner that each UID reads afterwards. Each object is made OPENABLE and opened by its
// name. The model mutex_cancel_counts covers the mutex cancel counts without opened UIDs, and
// rwlock_cancel_order covers the rwlock flags. The cases:
// - a mutex with an init count of 1, so the main thread owns it through the creating UID, with a
//   waiter that locks it, cancelled to -1 through the opened UID
// - a rwlock write locked by the main thread through the creating UID, with a reader that waits for
//   a read lock, cancelled with flag 1 through the opened UID, then unlocked through the creating UID
// - a fresh rwlock like the one before, cancelled with flag 0 through the opened UID
// Every wait has a timeout of 2 seconds. Each waiter is joined before the state is read, and the
// counts and owners read through the two UIDs are compared.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "Cancel lock"
#define NOT_DONE 0xDEAD

static SceUID wait_uid;
static volatile int wait_result;

// Locks the mutex under test and keeps the result
static int mutex_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelLockMutex(wait_uid, 1, &timeout);
    return 0;
}

// Takes a read lock on the rwlock under test and keeps the result
static int reader_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelLockReadRWLock(wait_uid, &timeout);
    return 0;
}

static int mutex_info(SceUID id, MutexInfo *info) {
    info->size = sizeof(*info);
    return sceKernelGetMutexInfo(id, (SceKernelMutexInfo *)info);
}

static void mutex_case(void) {
    const char *what = "mutex, cancel to -1 through the opened UID";
    const SceUID creating = sceKernelCreateMutex(NAME, SCE_KERNEL_ATTR_OPENABLE, 1, NULL);
    const SceUID opened = sceKernelOpenMutex(NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "%s: create and open the mutex", what);

    wait_uid = creating;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(mutex_waiter);
    test_wait_until_waiting(thread);
    int waiting = NOT_DONE;
    const int canceled = sceKernelCancelMutex(opened, -1, &waiting);
    test_join_thread(thread);

    MutexInfo through_creating = {0}, through_opened = {0};
    mutex_info(creating, &through_creating);
    mutex_info(opened, &through_opened);
    PRINTF(canceled, "%s: the cancel", what);
    PRINTF(waiting, "%s: the waiters it counted", what);
    PRINTF(wait_result, "%s: the waiter's lock", what);
    PRINTF(through_creating.currentCount, "%s: the count after, through the creating UID", what);
    PRINTF(through_opened.currentCount == through_creating.currentCount,
           "%s: the count through the opened UID is the same", what);
    PRINTF(through_creating.currentOwnerId == sceKernelGetThreadId(), "%s: the main thread owns it", what);
    PRINTF(through_opened.currentOwnerId == through_creating.currentOwnerId,
           "%s: the owner through the opened UID is the same", what);
    PRINTF(through_creating.numWaitThreads, "%s: the waiters after", what);

    sceKernelCloseMutex(opened);
    sceKernelDeleteMutex(creating);
}

static void rwlock_case(const char *what, SceInt32 flag) {
    const SceUID creating = sceKernelCreateRWLock(NAME, SCE_KERNEL_ATTR_OPENABLE, NULL);
    const SceUID opened = sceKernelOpenRWLock(NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "%s: create and open the rwlock", what);
    PRINTF(sceKernelLockWriteRWLock(creating, NULL), "%s: the write lock through the creating UID", what);

    wait_uid = creating;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(reader_waiter);
    test_wait_until_waiting(thread);
    SceUInt32 readers = NOT_DONE, writers = NOT_DONE;
    const int canceled = sceKernelCancelRWLock(opened, &readers, &writers, flag);
    test_join_thread(thread);

    SceKernelRWLockInfo through_creating = {.size = sizeof(through_creating)};
    SceKernelRWLockInfo through_opened = {.size = sizeof(through_opened)};
    sceKernelGetRWLockInfo(creating, &through_creating);
    sceKernelGetRWLockInfo(opened, &through_opened);
    PRINTF(canceled, "%s: the cancel", what);
    PRINTF(readers, "%s: the readers it counted", what);
    PRINTF(writers, "%s: the writers it counted", what);
    PRINTF(wait_result, "%s: the reader's lock", what);
    PRINTF(through_creating.lockCount, "%s: the lock count after, through the creating UID", what);
    PRINTF(through_opened.lockCount == through_creating.lockCount,
           "%s: the lock count through the opened UID is the same", what);
    PRINTF(through_creating.writeOwnerId == sceKernelGetThreadId(), "%s: the main thread owns it", what);
    PRINTF(through_opened.writeOwnerId == through_creating.writeOwnerId,
           "%s: the owner through the opened UID is the same", what);
    PRINTF(sceKernelUnlockWriteRWLock(creating), "%s: the write unlock through the creating UID", what);

    sceKernelCloseRWLock(opened);
    sceKernelDeleteRWLock(creating);
}

int main(void) {
    mutex_case();
    rwlock_case("rwlock, cancel with flag 1 through the opened UID", 1);
    rwlock_case("rwlock, cancel with flag 0 through the opened UID", 0);
    test_exit();
    return 0;
}
