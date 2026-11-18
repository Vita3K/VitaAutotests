// The exact number of threads a process can create before the thread limit error. Threads that
// never start are created until a create fails, then deleted. The count is taken again while one
// helper thread is alive and waiting, and again while it has ended but is not deleted. Each later
// count is one lower than the first, so the limit counts started threads too.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define CAP 256

static SceUID threads[CAP];
static SceUID sema;

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int helper(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 5 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Creates threads until one fails, deletes them, and returns how many were made
static int count_creates(int *error) {
    int count = 0;
    *error = 0;
    while (count < CAP) {
        const SceUID thread = sceKernelCreateThread("count", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
        if (thread < 0) {
            *error = thread;
            break;
        }
        threads[count++] = thread;
    }
    for (int i = 0; i < count; i++)
        sceKernelDeleteThread(threads[i]);
    return count;
}

int main(void) {
    sema = sceKernelCreateSema("helper", 0, 0, 1, NULL);

    int error = 0;
    const int base = count_creates(&error);
    PRINT("the error that ended the creates", error);
    PRINT("creates before the error", base);

    const SceUID thread = sceKernelCreateThread("helper", helper, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
    PRINT("create the helper", test_created(thread));
    PRINT("start the helper", sceKernelStartThread(thread, 0, NULL));
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    PRINT("the helper waits on the semaphore", test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    const int running = count_creates(&error);
    PRINT("the error with the helper waiting", error);
    PRINT("creates with the helper waiting", running);
    PRINT("one fewer than without the helper", running == base - 1);

    PRINT("release the helper", sceKernelSignalSema(sema, 1));
    SceUInt timeout = TEST_WAIT_US;
    PRINT("wait for its end", sceKernelWaitThreadEnd(thread, NULL, &timeout));
    const int ended = count_creates(&error);
    PRINT("the error with the helper ended", error);
    PRINT("creates with the helper ended and not deleted", ended);
    PRINT("one fewer than without the helper after its end", ended == base - 1);

    sceKernelDeleteThread(thread);
    const int after = count_creates(&error);
    PRINT("creates after deleting the helper", after);
    PRINT("the same as the first count", after == base);

    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
