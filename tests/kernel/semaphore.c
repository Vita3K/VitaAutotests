// Semaphore counts and waits
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID sema;
static int wait_result;

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    wait_result = sceKernelWaitSema(sema, 1, NULL);
    return 0;
}

// Starts a thread that waits for one count, and returns once it waits
static SceUID start_waiter(void) {
    wait_result = -1;
    const SceUID thread = test_start_thread(wait_thread);
    test_wait_until_waiting(thread);
    return thread;
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 2, NULL);

    PRINT("poll with nothing to take", sceKernelPollSema(sema, 1));
    PRINT("poll for a count of 0", sceKernelPollSema(sema, 0));
    // With a timeout so a missing check fails instead of waiting forever
    SceUInt timeout = 1000;
    PRINT("wait for more than the maximum", sceKernelWaitSema(sema, 3, &timeout));
    PRINT("signal up to the maximum", sceKernelSignalSema(sema, 2));
    PRINT("signal past the maximum", sceKernelSignalSema(sema, 1));
    PRINT("poll everything", sceKernelPollSema(sema, 2));

    timeout = 1000;
    PRINT("wait times out", sceKernelWaitSema(sema, 1, &timeout));
    PRINT("a timed out wait leaves no time", timeout);

    SceUID thread = start_waiter();
    sceKernelSignalSema(sema, 1);
    test_join_thread(thread);
    PRINT("a signal ends a wait", wait_result);

    int waiting = -1;
    PRINT("cancel to more than the maximum", sceKernelCancelSema(sema, 3, &waiting));
    thread = start_waiter();
    PRINT("cancel", sceKernelCancelSema(sema, 1, &waiting));
    test_join_thread(thread);
    PRINT("cancel counts the waiting threads", waiting);
    PRINT("a canceled wait", wait_result);
    PRINT("cancel sets the count", sceKernelPollSema(sema, 1));

    thread = start_waiter();
    PRINT("delete", sceKernelDeleteSema(sema));
    test_join_thread(thread);
    PRINT("a wait on a deleted semaphore", wait_result);

    test_exit();
    return 0;
}
