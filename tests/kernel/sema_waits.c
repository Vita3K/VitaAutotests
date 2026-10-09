// Semaphore waits across threads: several waiters, deleting, and the time left
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID sema;
static int wait_result[3];
static SceUInt time_left[3];

// Waits for one count into slot i
static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 500000;
    wait_result[i] = sceKernelWaitSema(sema, 1, &timeout);
    time_left[i] = timeout;
    return 0;
}

static void start_waiters(SceUID threads[3]) {
    for (int i = 0; i < 3; i++) {
        wait_result[i] = -1;
        threads[i] = test_start_thread_arg(wait_thread, i);
        test_wait_until_waiting(threads[i]);
    }
}

static int ended(void) {
    return (wait_result[0] == 0) + (wait_result[1] == 0) + (wait_result[2] == 0);
}

int main(void) {
    SceUID threads[3];
    sema = sceKernelCreateSema("sema", 0, 0, 3, NULL);

    // One signal for two counts ends two of three waits
    start_waiters(threads);
    sceKernelSignalSema(sema, 2);
    test_let_threads_wait();
    PRINT("a signal of 2 ends two waits", ended());
    PRINT("nothing is left for the third", sceKernelPollSema(sema, 1));
    sceKernelSignalSema(sema, 1);
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    PRINT("the next signal ends the third", ended());
    PRINT("a wait that ended early has time left", time_left[0] > 0 && time_left[0] < 500000);

    // Deleting ends every wait
    start_waiters(threads);
    PRINT("delete", sceKernelDeleteSema(sema));
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    PRINT("the first wait on the deleted semaphore", wait_result[0]);
    PRINT("the second", wait_result[1]);
    PRINT("the third", wait_result[2]);

    test_exit();
    return 0;
}
