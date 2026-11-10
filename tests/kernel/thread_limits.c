// How many threads a process can have at once. Threads with small stacks are created until one
// fails or the cap is reached, then all are deleted and creating works again. Then a number of
// threads run at once, each waiting on a semaphore, before they are released and joined. Counts
// are printed as thresholds so they don't depend on what else uses memory.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CREATE_CAP 512
#define RUNNERS 128

static SceUID threads[CREATE_CAP];
static SceUID sema;
static volatile int entered;
static volatile int runner_result[RUNNERS];

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Counts itself, then waits for the semaphore. The timeout is long, since all runners have to
// start and wait before the test releases them.
static int runner(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    __atomic_fetch_add(&entered, 1, __ATOMIC_SEQ_CST);
    SceUInt timeout = 5 * TEST_WAIT_US;
    runner_result[i] = sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Returns whether the first count runners all wait on the semaphore
static int all_waiting(int count) {
    for (int i = 0; i < count; i++) {
        if (!test_in_wait(threads[i], WAIT_TYPE_SEMA, sema))
            return 0;
    }
    return 1;
}

int main(void) {
    // Creating until it fails
    int created = 0, create_error = 0;
    while (created < CREATE_CAP) {
        const SceUID thread = sceKernelCreateThread("limit", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
        if (thread < 0) {
            create_error = thread;
            break;
        }
        threads[created++] = thread;
    }
    PRINT("the error that ended the creation, 0 if the cap was reached", create_error);
    PRINT("at least 16 threads created", created >= 16);
    PRINT("at least 64 threads created", created >= 64);
    PRINT("at least 128 threads created", created >= 128);
    PRINT("at least 256 threads created", created >= 256);
    PRINT("at least 512 threads created", created >= 512);
    PRINT("every thread up to the cap created", created == CREATE_CAP);
    int deleted = 0;
    for (int i = 0; i < created; i++)
        deleted += sceKernelDeleteThread(threads[i]) == 0;
    PRINT("every created thread deleted", deleted == created);

    // Creating after all were deleted
    SceUID thread = sceKernelCreateThread("again", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
    PRINT("create after deleting them", test_created(thread));
    if (thread >= 0) {
        PRINT("start it", sceKernelStartThread(thread, 0, NULL));
        SceUInt timeout = TEST_WAIT_US;
        PRINT("wait for its end", sceKernelWaitThreadEnd(thread, NULL, &timeout));
        sceKernelDeleteThread(thread);
    }

    // Many threads waiting at once
    sema = sceKernelCreateSema("runners", 0, 0, RUNNERS, NULL);
    int started = 0, start_error = 0;
    for (int i = 0; i < RUNNERS; i++) {
        runner_result[i] = 0xDEAD;
        thread = sceKernelCreateThread("runner", runner, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
        if (thread < 0) {
            start_error = thread;
            break;
        }
        const int result = sceKernelStartThread(thread, sizeof(i), &i);
        if (result < 0) {
            start_error = result;
            sceKernelDeleteThread(thread);
            break;
        }
        threads[started++] = thread;
    }
    PRINT("the error that ended the starts, 0 if every runner started", start_error);
    PRINTF(started == RUNNERS, "all %d runners started", RUNNERS);
    TEST_WAIT_FOR(entered == started);
    PRINT("every started runner ran", entered == started);
    TEST_WAIT_FOR(all_waiting(started));
    PRINT("every started runner waits on the semaphore", all_waiting(started));
    PRINT("release them", sceKernelSignalSema(sema, started));
    int ended = 0, woken = 0;
    for (int i = 0; i < started; i++) {
        SceUInt timeout = TEST_WAIT_US;
        if (sceKernelWaitThreadEnd(threads[i], NULL, &timeout) == 0)
            ended++;
        woken += runner_result[i] == 0;
        sceKernelDeleteThread(threads[i]);
    }
    PRINT("every runner ended", ended == started);
    PRINT("every runner got the semaphore", woken == started);
    sceKernelDeleteSema(sema);

    test_exit();
    return 0;
}
