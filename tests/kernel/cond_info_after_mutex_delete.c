// The info of a condition variable while a thread waits on it, and after its mutex was deleted
// during the wait. W locks the mutex and waits on the condition with a 2 s timeout. The main thread
// reads the condition's info while W waits, deletes the mutex, joins W and reads the info again.
// The test prints the info's result, the mutex it names and the waiting thread count before the
// delete, the delete's result, W's result, the info's result after the delete, whether the info
// still names the mutex and whether it names -1, the waiting thread count after, and whether the
// attribute is unchanged. The info of a condition with waiters is read in info_fields, and
// cond_mutex_delete deletes the mutex during the wait without reading the info.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID mutex, cond;
static volatile int waiter_result = 0xDEAD;

// Waits on the condition with the mutex, then gives back whatever the wait leaves it holding
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &timeout);
    timeout = TEST_WAIT_US;
    waiter_result = sceKernelWaitCond(cond, &timeout);
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

int main(void) {
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", SCE_KERNEL_ATTR_THREAD_PRIO, mutex, NULL);
    const SceUID w = test_start_thread(waiter);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_COND);

    SceKernelCondInfo before = { .size = sizeof(before) };
    PRINT("info before the delete: the result", sceKernelGetCondInfo(cond, &before));
    PRINT("info before the delete: the mutex is the mutex", before.mutexId == mutex);
    PRINT("info before the delete: the waiting threads", before.numWaitThreads);
    PRINT("the mutex's delete during the wait", sceKernelDeleteMutex(mutex));
    test_join_thread(w);

    SceKernelCondInfo after = { .size = sizeof(after) };
    PRINT("info after the delete: the result", sceKernelGetCondInfo(cond, &after));
    PRINT("info after the delete: the mutex is the mutex", after.mutexId == mutex);
    PRINT("info after the delete: the mutex is -1", after.mutexId == -1);
    PRINT("info after the delete: the waiting threads", after.numWaitThreads);
    PRINT("info after the delete: the attribute is unchanged", after.attr == before.attr);
    PRINT("W's wait result", waiter_result);

    sceKernelDeleteCond(cond);
    test_exit();
    return 0;
}
