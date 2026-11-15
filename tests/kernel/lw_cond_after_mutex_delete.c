// What a lightweight condition variable does after its lightweight mutex was deleted while the
// condition variable lives on. The main thread creates the mutex and the condition variable, deletes
// the mutex, and calls the condition variable with nobody waiting on it. The test prints a wait with
// a timeout of TEST_WAIT_US, a wait with callbacks, a signal, a signal all, a signal to this thread, and
// the info by work area and by UID. The info prints its result, the number of waiting threads, and
// whether the mutex it names is the deleted work area or NULL. Heavy condition variables are covered
// by cond_error_time_left and cond_info_after_mutex_delete, and lw_cond_errors signals a
// lightweight one once.
//
// On a PS Vita TV on firmware 3.65 both waits fail at once with 0x80028181, the unknown lightweight
// mutex error, and leave the timeout as it was. Signal and signal all return 0 with no waiter, and a
// signal to this thread fails with 0x80028022. Both infos still name the deleted mutex's work area.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceKernelLwMutexWork mutex;
static SceKernelLwCondWork cond;

static void print_info(const char *name, int result, const LwCondInfo *info) {
    PRINTF(result, "%s: the result", name);
    PRINTF(info->numWaitThreads, "%s: the waiting threads", name);
    PRINTF(info->lwMutex == &mutex, "%s: the mutex is the deleted work area", name);
    PRINTF(info->lwMutex == NULL, "%s: the mutex is NULL", name);
}

int main(void) {
    sceKernelCreateLwMutex(&mutex, "mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&cond, "cond", 0, &mutex, NULL);
    LwCondInfo before = { .size = sizeof(before) };
    const int before_result = sceKernelGetLwCondInfo(&cond, &before);
    const SceUID uid = before.uid;
    print_info("info before the delete", before_result, &before);
    PRINT("delete the mutex", sceKernelDeleteLwMutex(&mutex));

    SceUInt32 timeout = TEST_WAIT_US;
    PRINT("wait", sceKernelWaitLwCond(&cond, &timeout));
    PRINT("wait: the timeout is unchanged", timeout == TEST_WAIT_US);
    timeout = TEST_WAIT_US;
    PRINT("wait with callbacks", sceKernelWaitLwCondCB(&cond, &timeout));
    PRINT("wait with callbacks: the timeout is unchanged", timeout == TEST_WAIT_US);
    PRINT("signal", sceKernelSignalLwCond(&cond));
    PRINT("signal all", sceKernelSignalLwCondAll(&cond));
    PRINT("signal to this thread", sceKernelSignalLwCondTo(&cond, sceKernelGetThreadId()));

    LwCondInfo by_work = { .size = sizeof(by_work) };
    print_info("info by work area", sceKernelGetLwCondInfo(&cond, &by_work), &by_work);
    LwCondInfo by_id = { .size = sizeof(by_id) };
    print_info("info by UID", sceKernelGetLwCondInfoById(uid, &by_id), &by_id);
    PRINT("info by UID names the same work area", by_id.work == by_work.work);

    PRINT("delete", sceKernelDeleteLwCond(&cond));
    test_exit();
    return 0;
}
