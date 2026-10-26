// Waits with timeouts of 2^31 and up, on a thread signal, a thread end and a semaphore. Each wait
// is ended by the test 300 ms after it starts, well before its timeout runs out, so the outcome
// never depends on the timeout expiring. For each wait the test prints the wait type it sees at
// 300 ms, whether the waiter ended within 4 seconds, its result, and two comparisons of the time
// left written back: whether it is below the timeout, and whether it is above the timeout less 5
// seconds. Together they show the timeout was counted down from about its full length.
// Cases: the three waits, each with timeouts 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE and 0xFFFFFFFF.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind { SIGNAL, THREAD_END, SEMA, KIND_COUNT };
static const char *const kind_names[] = { "signal wait", "thread end wait", "semaphore wait" };

static const SceUInt32 timeouts[] = { 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF };

// How long a wait runs before the test ends it, and how far below its timeout the time left may be
#define END_AFTER_US 300000
#define SLACK_US 5000000

static int kind;
static SceUInt timeout, time_left;
static int wait_result;
static SceUID gate, target;

// Waits for the gate to be signaled, then ends with a status
static int target_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt gate_timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &gate_timeout);
    return 0x42;
}

// Waits in the kind of wait the test picked, with the timeout the test set
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    time_left = timeout;
    switch (kind) {
    case SIGNAL: wait_result = sceKernelWaitSignal(0, 0, &time_left); break;
    case THREAD_END: wait_result = sceKernelWaitThreadEnd(target, NULL, &time_left); break;
    default: wait_result = sceKernelWaitSema(gate, 1, &time_left); break;
    }
    return 0;
}

static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : 0;
}

// Waits up to 4 seconds for the thread to end and deletes it if it did, so a wait that isn't
// ended doesn't hang the test
static int join_bounded(SceUID thread) {
    SceUInt join_timeout = 4 * TEST_WAIT_US;
    const int ended = sceKernelWaitThreadEnd(thread, NULL, &join_timeout) == 0;
    if (ended)
        sceKernelDeleteThread(thread);
    return ended;
}

// Starts a wait of the kind with the timeout, ends it after 300 ms and prints what it saw
static void run_wait(int wait_kind, SceUInt32 wait_timeout) {
    kind = wait_kind;
    timeout = wait_timeout;
    wait_result = 0xDEAD;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    if (kind == THREAD_END) {
        target = test_start_thread(target_thread);
        TEST_WAIT_FOR(wait_type(target) != 0);
    }
    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(wait_type(thread) != 0);
    sceKernelDelayThread(END_AFTER_US);
    PRINTF(wait_type(thread), "%s with timeout 0x%X: its wait type at 300 ms", kind_names[kind],
           (unsigned)timeout);
    if (kind == SIGNAL)
        sceKernelSendSignal(thread);
    else
        sceKernelSignalSema(gate, 1);
    const int ended = join_bounded(thread);
    PRINTF(ended, "%s with timeout 0x%X: the waiter ended", kind_names[kind], (unsigned)timeout);
    PRINTF(wait_result, "%s with timeout 0x%X: its result", kind_names[kind], (unsigned)timeout);
    PRINTF(time_left < timeout, "%s with timeout 0x%X: its time left is below the timeout",
           kind_names[kind], (unsigned)timeout);
    PRINTF((SceUInt64)time_left + SLACK_US > timeout,
           "%s with timeout 0x%X: its time left is above the timeout less 5 s", kind_names[kind],
           (unsigned)timeout);
    if (kind == THREAD_END)
        test_join_thread(target);
    sceKernelDeleteSema(gate);
}

int main(void) {
    for (int wait_kind = 0; wait_kind < KIND_COUNT; wait_kind++) {
        for (unsigned int i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); i++)
            run_wait(wait_kind, timeouts[i]);
    }

    test_exit();
    return 0;
}
