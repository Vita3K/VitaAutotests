// sceKernelGetThreadRunStatus while another thread is in a wait. A target waits in a delay, then
// on a semaphore, an event flag and a lightweight mutex, one at a time, each with a timeout of 2
// seconds. The main thread reads the run status while each wait is on, then joins the target when
// its timeout ended the wait. For each wait the test prints the call's result, whether the target
// is in its wait at the read, whether an entry holds the target, and whether an entry holds the
// main thread. The lightweight mutex is held by the main thread while the target waits on it. The
// wait kinds are the ones sceKernelGetThreadInfo reports in thread_info.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind { DELAY, SEMA, EVENT_FLAG, LW_MUTEX };

static SceUID sema, flag;
static SceKernelLwMutexWork lw_mutex;

// Waits in the kind given as the argument, with the timeout
static int wait_entry(SceSize args, void *argp) {
    (void)args;
    SceUInt timeout = TEST_WAIT_US;
    unsigned int bits;
    switch (*(const int *)argp) {
    case DELAY: sceKernelDelayThread(TEST_WAIT_US); break;
    case SEMA: sceKernelWaitSema(sema, 1, &timeout); break;
    case EVENT_FLAG: sceKernelWaitEventFlag(flag, 1, SCE_EVENT_WAITAND, &bits, &timeout); break;
    case LW_MUTEX: sceKernelLockLwMutex(&lw_mutex, 1, &timeout); break;
    }
    return 0;
}

// Returns whether the thread is in a wait at the moment of the call
static int in_a_wait(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType != 0;
}

// Returns how many entries of the run status hold the thread
static int holders(const SceKernelThreadRunStatus *status, SceUID thread) {
    int count = 0;
    for (int i = 0; i < 4; i++)
        count += status->cpuInfo[i].threadId == thread;
    return count;
}

// Starts a target waiting in the kind, reads the run status while it waits, then joins it
static void run(const char *name, int kind) {
    const SceUID target = test_start_thread_arg(wait_entry, kind);
    test_wait_until_waiting(target);

    SceKernelThreadRunStatus status = { .size = sizeof(status) };
    const int result = get_thread_run_status(&status);
    PRINTF(result, "%s: the call", name);
    PRINTF(in_a_wait(target), "%s: the target is in its wait", name);
    PRINTF(holders(&status, target) > 0, "%s: an entry holds the target", name);
    PRINTF(holders(&status, sceKernelGetThreadId()) > 0, "%s: an entry holds the caller", name);
    test_join_thread(target);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    sceKernelCreateLwMutex(&lw_mutex, "lw_mutex", 0, 0, NULL);

    run("delay", DELAY);
    run("semaphore", SEMA);
    run("event flag", EVENT_FLAG);

    // Held, so the target's lock waits until its timeout
    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    run("lightweight mutex", LW_MUTEX);
    sceKernelUnlockLwMutex(&lw_mutex, 1);

    test_exit();
    return 0;
}
