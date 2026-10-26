// Whether a callback notified from another core reaches a thread that waits with callbacks on core
// 0 within 250 ms, and what the wait does to a spinner on core 0 meanwhile. The spinner S counts in
// a loop without syscalls, and the thread T waits on a semaphore with callbacks on core 0. The main
// thread, on core 2, notifies T's callback. The callback notes the process time, the core it runs
// on and S's counter, then spins until the main thread lets it return, so T keeps core 0 while the
// main thread reads. The main thread reads 300 ms after the notify, without printing before that:
// - whether the callback ran within 250 ms of the notify
// - whether S's counter stopped moving, that is whether it is the same as when the callback started
// - the core the callback ran on
//
// After the reads the main thread lets the callback return, signals the semaphore to end T's wait,
// and stops S. The cases are T at 0x60 above S at 0x70, and T at 0xA0 above S at 0xB0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define NOTIFY_WINDOW_US 300000
#define CALLBACK_WINDOW_US 250000

static SceUID sema, callback;
static volatile int spinner_up, spinner_stop, callback_ran, callback_release, t_wait_result;
static volatile SceUInt64 callback_time;
static volatile int callback_cpu;
static volatile unsigned int spinner_count, callback_spinner_count;

// S counts without syscalls until the case ends
static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    spinner_up = 1;
    while (!spinner_stop)
        spinner_count = spinner_count + 1;
    return 0;
}

// Notes what it sees, then spins until the main thread has read the state
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_time = sceKernelGetProcessTimeWide();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(sceKernelGetThreadId(), &info);
    callback_cpu = info.currentCpuId;
    callback_spinner_count = spinner_count;
    callback_ran = 1;
    while (!callback_release)
        ;
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    SceUInt timeout = TEST_WAIT_US;
    t_wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    sceKernelDeleteCallback(callback);
    return 0;
}

// Returns whether the thread info says that its thread waits with callbacks on the semaphore
static int t_waits(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == (WAIT_TYPE_SEMA | WAIT_TYPE_CB) &&
           info.waitId == sema;
}

static void run_case(const char *name, int t_priority, int s_priority) {
    spinner_up = spinner_stop = callback_ran = callback_release = 0;
    t_wait_result = 0xDEAD;
    callback_time = 0;
    callback_cpu = -1;
    spinner_count = callback_spinner_count = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);

    const SceUID spinner =
        sceKernelCreateThread("spinner", spinner_entry, s_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(spinner, 0, NULL);
    TEST_WAIT_FOR(spinner_up);

    const SceUID t = sceKernelCreateThread("t", t_entry, t_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(t_waits(t));

    // Nothing is printed between the notify and the reads
    const SceUInt64 notify_time = sceKernelGetProcessTimeWide();
    const int notify = sceKernelNotifyCallback(callback, 1);
    sceKernelDelayThread(NOTIFY_WINDOW_US);
    const int ran = callback_ran;
    const int ran_in_window = ran && callback_time - notify_time < CALLBACK_WINDOW_US;
    const int spinner_held = ran && spinner_count == callback_spinner_count;
    const int cpu = callback_cpu;

    callback_release = 1;
    sceKernelSignalSema(sema, 1);
    spinner_stop = 1;
    test_join_thread(t);
    test_join_thread(spinner);
    sceKernelDeleteSema(sema);

    PRINTF(notify, "%s: the notify", name);
    PRINTF(ran_in_window, "%s: the callback ran within 250 ms", name);
    PRINTF(spinner_held, "%s: S's loop counter stopped moving", name);
    PRINTF(cpu, "%s: the core the callback ran on", name);
    PRINTF(t_wait_result, "%s: T's wait", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("T at 0x60, S at 0x70", 0x60, 0x70);
    run_case("T at 0xA0, S at 0xB0", 0xA0, 0xB0);

    test_exit();
    return 0;
}
