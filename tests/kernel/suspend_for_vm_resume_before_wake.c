// Whether sceKernelResumeThreadForVM undoes a suspend of a thread that is still in its wait, so a
// later signal wakes the thread as usual. The waiter W has the VM attribute and runs on core 0, and
// the main thread on core 1 suspends it while it waits on a semaphore. Each case then resumes W
// once, with no wake in between, and signals the semaphore:
// - suspended once: W's status and wait type while suspended and after the resume, then after the
//   signal whether W went on, its wait's result, the semaphore's count and waiters, and W's end
//   status and exit status
// - suspended twice: the same, but after the signal W must not have gone on, since one suspend is
//   left. Its status and wait type, the semaphore's count and waiters at that point, then the
//   second resume and the same results after it
// The main thread waits 200 ms after the signal to see that W did not run.
//
// On a PS Vita TV on firmware 3.65 a suspended W keeps showing WAITING 0x8 with wait type 0x20, and
// so does it after the first resume. Suspended once, the suspend returns 1, the resume returns 0
// and W goes on within 200 ms of the signal. Its wait returns 0, its status is 0x10 and its exit
// status is 0x55. Suspended twice, the suspends return 1 and 2 and the first resume returns 1.
// After the signal W has not gone on and shows SUSPENDED 0x100 with wait type 0, and the semaphore
// has count 0 and no waiters. The second resume returns 0, and then W goes on with the same results
// as suspended once.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define W_PRIORITY 0xA0
#define W_EXIT_STATUS 0x55
// How long the main thread waits for a thread that mustn't go on to show it doesn't
#define HELD_US 200000

static SceUID sema;
static volatile int woke, wait_result;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    wait_result = sceKernelWaitSema(sema, 1, &timeout);
    woke = 1;
    return W_EXIT_STATUS;
}

static void print_wait_state(const char *name, const char *when, SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINTF(info.status, "%s: its status %s", name, when);
    PRINTF(info.waitType, "%s: its wait type %s", name, when);
}

static void print_sema(const char *name, const char *when) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    PRINTF(info.currentCount, "%s: the count %s", name, when);
    PRINTF(info.numWaitThreads, "%s: the waiters %s", name, when);
}

static void run_case(const char *name, int suspends) {
    woke = 0;
    wait_result = 0xDEAD;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID w = sceKernelCreateThread("waiter", waiter, W_PRIORITY, 0x4000, THREAD_ATTR_VM,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_SEMA, sema));
    for (int i = 0; i < suspends; i++)
        PRINTF(sceKernelSuspendThreadForVM(w), "%s: suspend %d", name, i + 1);
    print_wait_state(name, "while suspended", w);
    PRINTF(sceKernelResumeThreadForVM(w), "%s: the first resume", name);
    print_wait_state(name, "after the first resume", w);
    PRINTF(sceKernelSignalSema(sema, 1), "%s: the signal", name);
    sceKernelDelayThread(HELD_US);
    PRINTF(woke, "%s: it went on within 200 ms of the signal", name);
    print_wait_state(name, "200 ms after the signal", w);
    print_sema(name, "200 ms after the signal");
    if (suspends > 1) {
        PRINTF(sceKernelResumeThreadForVM(w), "%s: the second resume", name);
        TEST_WAIT_FOR(woke);
        PRINTF(woke, "%s: it went on after the second resume", name);
    }
    PRINTF(wait_result, "%s: its wait's result", name);
    print_sema(name, "at the end");
    SceUInt timeout = TEST_WAIT_US;
    int exit_status = 0xDEAD;
    PRINTF(sceKernelWaitThreadEnd(w, &exit_status, &timeout), "%s: the wait for its end", name);
    PRINTF(exit_status, "%s: its exit status", name);
    print_wait_state(name, "after its end", w);
    sceKernelDeleteThread(w);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case("suspended once", 1);
    run_case("suspended twice", 2);
    test_exit();
    return 0;
}
