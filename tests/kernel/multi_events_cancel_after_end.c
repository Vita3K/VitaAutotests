// What a cancel of an event reports for a multiple event wait that another of its events already
// ended. Every thread runs on core 0, the waiter T at 0xB0 below the main thread at 0x60, so T can't
// run while the main thread does. T waits with OR on the simple events A and B, with a timeout of
// 2 s. The cases:
// - the main thread sets A, which ends T's wait, then cancels B before T ran
// - the main thread cancels B while T still waits, for reference
// Each prints the cancel's result and the waiters it reports, then lets T run and prints T's call,
// whether its first entry is A or B (1 for A, 2 for B, 0 for another) and that entry's result.
//
// On a PS Vita TV on firmware 3.65 the cancel after the set reports 0 waiters, since B's entry
// belongs to a wait that ended, and T gets one entry, A's, with 0. The cancel while T waits reports
// 1 waiter and ends T's wait with one entry, B's, with 0x80028007 (WAIT_CANCEL).
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define T_PRIORITY 0xB0
#define MULTI_EVENTS_WAIT_ID ((SceUID)0x80000000)

static SceUID a, b;
static volatile int call_result;
static ResultEvent results[2];

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    WaitEvent list[2] = { { a, EVENT_BIT_A }, { b, EVENT_BIT_A } };
    SceUInt32 timeout = 2000000;
    call_result = sceKernelWaitMultipleEvents(list, 2, EVENT_WAIT_MODE_OR, results, &timeout);
    return 0;
}

static void run(const char *name, int set_a) {
    a = sceKernelCreateSimpleEvent("a", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    b = sceKernelCreateSimpleEvent("b", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    call_result = 0xDEAD;
    sceClibMemset(results, 0, sizeof(results));
    const SceUID t = sceKernelCreateThread("t", waiter, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_MP_EVENTS, MULTI_EVENTS_WAIT_ID));
    if (set_a)
        sceKernelSetEvent(a, EVENT_BIT_A, 0);
    SceUInt32 waiters = 0xDEAD;
    const int cancelled = sceKernelCancelEvent(b, &waiters);
    test_join_thread(t);
    PRINTF(cancelled, "%s: the cancel", name);
    PRINTF(waiters, "%s: the waiters it reports", name);
    PRINTF(call_result, "%s: T's call", name);
    PRINTF(results[0].eventId == a ? 1 : results[0].eventId == b ? 2 : 0, "%s: T's first entry", name);
    PRINTF(results[0].result, "%s: its result", name);
    sceKernelDeleteSimpleEvent(a);
    sceKernelDeleteSimpleEvent(b);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    run("a cancel after another event ended the wait", 1);
    run("a cancel while the wait goes on", 0);
    test_exit();
    return 0;
}
