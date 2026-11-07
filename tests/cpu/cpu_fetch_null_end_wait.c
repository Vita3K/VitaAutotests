// A call to address 0 in ARM state by a worker that the main thread waits on with
// sceKernelWaitThreadEnd, not on a semaphore like the other cpu_fetch tests do. The main thread
// prints a line, starts the worker and waits for its end with a long timeout. The worker makes the
// call only once the main thread waits for its end, and else returns at once. Only once the wait
// returned does the main thread print its result, the worker's exit status, whether the worker saw
// the wait, and a last line that shows it went on. So a fault that ends the app leaves the output
// at the line before the start, even though the worker stops running at once.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID main_thread;
static volatile int saw_wait;

static int worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    TEST_WAIT_FOR(test_in_wait(main_thread, WAIT_TYPE_THREAD_END, sceKernelGetThreadId()));
    saw_wait = test_in_wait(main_thread, WAIT_TYPE_THREAD_END, sceKernelGetThreadId());
    if (!saw_wait)
        return 7;
    register SceUInt32 r0 __asm__("r0") = 0x1234;
    register SceUInt32 r1 __asm__("r1") = 0;
    __asm__ volatile("blx r1" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "lr", "memory", "cc");
    return 7;
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    const SceUID thread = sceKernelCreateThread("worker", worker, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("create the worker", thread > 0);
    PRINT("start the worker", 1);
    sceKernelStartThread(thread, 0, NULL);
    int status = 0xDEAD;
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINT("the wait for the worker's end", sceKernelWaitThreadEnd(thread, &status, &timeout));
    PRINT("its exit status", status);
    PRINT("the worker saw the wait", saw_wait);
    PRINT("the main thread goes on", 1);
    test_exit();
    return 0;
}
