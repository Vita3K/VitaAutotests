// What sceKernelSuspendThreadForVM and sceKernelResumeThreadForVM do with
// threads created without the VM attribute, which suspend_for_vm_edges leaves
// out since it only uses threads with it. The main thread runs on core 1, and
// the threads it suspends run on core 0. The cases:
// - a spinner at 0x70: the suspend's result, its status before and after it, whether it makes
//   progress over 250 ms while suspended, the resume's result, whether it makes progress over
//   250 ms after the resume, its status then, and the result of a second resume, made on a thread
//   that isn't suspended
// - a waiter on a semaphore at 0x70: the suspend's result, its status before and after it, the
//   signal's result, whether it woke within 250 ms of the signal and before the resume, the
//   resume's result, and its wait's result
// - the same spinner and waiter with the VM attribute, as the control
// Each case resumes its thread before it joins it, so a suspend that holds a thread can't hang the
// test.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define SPIN_PRIORITY 0x70
#define WAIT_PRIORITY 0x70

static SceUID sema;
static volatile int spin_ran, spin_stop, woke, wait_result;
static volatile unsigned int spin_count;

// Counts until spin_stop is set
static int spinner(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spin_ran = 1;
    while (!spin_stop)
        spin_count++;
    return 0;
}

// Waits for the semaphore, and records what the wait returned
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    wait_result = sceKernelWaitSema(sema, 1, &timeout);
    woke = 1;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// Whether the spinner's count moves within 250 ms
static int progresses(void) {
    const unsigned int before = spin_count;
    sceKernelDelayThread(250000);
    return spin_count != before;
}

static void spin_case(const char *name, SceUInt32 attr) {
    spin_ran = spin_stop = 0;
    spin_count = 0;
    const SceUID t = sceKernelCreateThread("spinner", spinner, SPIN_PRIORITY, 0x4000, attr,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(spin_ran);
    const SceUInt32 before = status_of(t);
    const int suspended = sceKernelSuspendThreadForVM(t);
    const SceUInt32 suspended_status = status_of(t);
    const int held = progresses();
    const int resumed = sceKernelResumeThreadForVM(t);
    const int runs = progresses();
    const SceUInt32 after = status_of(t);
    const int resumed_again = sceKernelResumeThreadForVM(t);
    spin_stop = 1;
    test_join_thread(t);
    PRINTF(before, "%s: its status before the suspend", name);
    PRINTF(suspended, "%s: the suspend", name);
    PRINTF(suspended_status, "%s: its status while suspended", name);
    PRINTF(held, "%s: it makes progress over 250 ms while suspended", name);
    PRINTF(resumed, "%s: the resume", name);
    PRINTF(runs, "%s: it makes progress over 250 ms after the resume", name);
    PRINTF(after, "%s: its status after the resume", name);
    PRINTF(resumed_again, "%s: a second resume, of a thread that isn't suspended", name);
}

static void wait_case(const char *name, SceUInt32 attr) {
    woke = 0;
    wait_result = 0xDEAD;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("waiter", waiter, WAIT_PRIORITY, 0x4000, attr,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema));
    const SceUInt32 before = status_of(t);
    const int suspended = sceKernelSuspendThreadForVM(t);
    const SceUInt32 suspended_status = status_of(t);
    const int signaled = sceKernelSignalSema(sema, 1);
    sceKernelDelayThread(250000);
    const int woke_held = woke;
    const int resumed = sceKernelResumeThreadForVM(t);
    test_join_thread(t);
    sceKernelDeleteSema(sema);
    PRINTF(before, "%s: its status before the suspend", name);
    PRINTF(suspended, "%s: the suspend", name);
    PRINTF(suspended_status, "%s: its status while suspended", name);
    PRINTF(signaled, "%s: the signal", name);
    PRINTF(woke_held, "%s: it woke within 250 ms of the signal, before the resume", name);
    PRINTF(resumed, "%s: the resume", name);
    PRINTF(wait_result, "%s: its wait's result", name);
}

int main(void) {
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    spin_case("spinner without VM", 0);
    wait_case("waiter without VM", 0);
    spin_case("spinner with VM, control", THREAD_ATTR_VM);
    wait_case("waiter with VM, control", THREAD_ATTR_VM);
    test_exit();
    return 0;
}
