// Whether a pending thread signal survives a thread's start, end and restart. The thread runs in
// the mode its start argument gives: it ends at once, it waits on a semaphore, or it calls
// WaitSignal once or twice. Every WaitSignal has timeout 0, so no call can hang. Each case uses
// fresh threads:
// - a: two signals are sent to a thread that never started, then it starts and calls WaitSignal
//   twice
// - b: a signal is sent while the thread waits on a semaphore, and the thread ends without taking
//   it. Then a signal is sent to the dormant thread, which starts again and calls WaitSignal once
// - c: the thread ends at once, a signal is sent to the dormant thread, and its next run calls
//   WaitSignal twice
// Each case prints the results of its sends, its starts and its WaitSignal calls.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MODE_END 0
#define MODE_WAIT_GO 1
#define MODE_SIGNAL_ONCE 2
#define MODE_SIGNAL_TWICE 3

static SceUID go;
static volatile int go_result, signal_results[2];

// Runs in the mode its argument gives, and records the results of its waits
static int signal_entry(SceSize args, void *argp) {
    (void)args;
    const int mode = *(const int *)argp;
    if (mode == MODE_WAIT_GO) {
        SceUInt semaphore_timeout = 2 * TEST_WAIT_US;
        go_result = sceKernelWaitSema(go, 1, &semaphore_timeout);
    }
    if (mode == MODE_SIGNAL_ONCE || mode == MODE_SIGNAL_TWICE) {
        SceUInt32 timeout = 0;
        signal_results[0] = sceKernelWaitSignal(0, 0, &timeout);
        timeout = 0;
        if (mode == MODE_SIGNAL_TWICE)
            signal_results[1] = sceKernelWaitSignal(0, 0, &timeout);
    }
    return 0;
}

static SceUID create_thread(void) {
    return sceKernelCreateThread("signal", signal_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
}

static int start_in(SceUID thread, int mode) {
    return sceKernelStartThread(thread, sizeof(mode), &mode);
}

static void reset(void) {
    go_result = 0xDEAD;
    signal_results[0] = signal_results[1] = 0xDEAD;
}

static void case_a(void) {
    reset();
    const SceUID thread = create_thread();
    PRINT("a: the first send to a thread that never started", sceKernelSendSignal(thread));
    PRINT("a: the second send before it starts", sceKernelSendSignal(thread));
    PRINT("a: start it", start_in(thread, MODE_SIGNAL_TWICE));
    test_join_thread(thread);
    PRINT("a: its first WaitSignal", signal_results[0]);
    PRINT("a: its second WaitSignal", signal_results[1]);
}

static void case_b(void) {
    reset();
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID thread = create_thread();
    PRINT("b: start it to wait on the semaphore", start_in(thread, MODE_WAIT_GO));
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA, go));
    PRINT("b: a signal sent while it waits on the semaphore", sceKernelSendSignal(thread));
    sceKernelSignalSema(go, 1);
    SceUInt end_timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &end_timeout);
    PRINT("b: its semaphore wait", go_result);
    PRINT("b: a signal sent to it once it ended", sceKernelSendSignal(thread));
    PRINT("b: start it again to call WaitSignal once", start_in(thread, MODE_SIGNAL_ONCE));
    test_join_thread(thread);
    PRINT("b: its WaitSignal in the new run", signal_results[0]);
    sceKernelDeleteSema(go);
}

static void case_c(void) {
    reset();
    const SceUID thread = create_thread();
    PRINT("c: start it to end at once", start_in(thread, MODE_END));
    SceUInt end_timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &end_timeout);
    PRINT("c: a signal sent to the dormant thread", sceKernelSendSignal(thread));
    PRINT("c: start it again to call WaitSignal twice", start_in(thread, MODE_SIGNAL_TWICE));
    test_join_thread(thread);
    PRINT("c: its first WaitSignal", signal_results[0]);
    PRINT("c: its second WaitSignal", signal_results[1]);
}

int main(void) {
    case_a();
    case_b();
    case_c();

    test_exit();
    return 0;
}
