// The waiting thread counts of two simple events while waiters sit in sceKernelWaitMultipleEvents.
// Both events are auto reset. Thread A waits in OR mode on E1 with EVENT_BIT_A and on E2 with
// EVENT_BIT_B. Thread B then waits in OR mode on E1 listed twice, both with EVENT_BIT_A. Both
// waits have a timeout of 3 seconds and no callbacks. Main reads the counts of both events after
// each wait starts, sets E2 and joins A, sets E1 and joins B, then reads the counts once more.
// Each set ends one wait, since A is gone before E1 is set. Prints the counts of both events at
// each step, the result of each set, and the result of each wait.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WAIT_US 3000000

static SceUID e1, e2;
static WaitEvent list_a[2], list_b[2];
static ResultEvent results_a[2], results_b[2];
static volatile int result_a, result_b;

// Waits for E1 and E2 in OR mode
static int waiter_a(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAIT_US;
    result_a = sceKernelWaitMultipleEvents(list_a, 2, EVENT_WAIT_MODE_OR, results_a, &timeout);
    return 0;
}

// Waits for E1 in OR mode, listed twice
static int waiter_b(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = WAIT_US;
    result_b = sceKernelWaitMultipleEvents(list_b, 2, EVENT_WAIT_MODE_OR, results_b, &timeout);
    return 0;
}

// Returns whether thread is in a wait of the given type
static int in_wait(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

// Returns the waiting thread count of the event
static SceUInt32 waiting_of(SceUID id) {
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(id, &info);
    return info.numWaitThreads;
}

// Prints the waiting thread count of E1 and E2
static void print_counts(const char *when) {
    PRINTF(waiting_of(e1), "%s: E1 waiting threads", when);
    PRINTF(waiting_of(e2), "%s: E2 waiting threads", when);
}

int main(void) {
    e1 = sceKernelCreateSimpleEvent("e1", EVENT_ATTR_AUTO_RESET, 0, NULL);
    e2 = sceKernelCreateSimpleEvent("e2", EVENT_ATTR_AUTO_RESET, 0, NULL);
    list_a[0] = (WaitEvent){ e1, EVENT_BIT_A };
    list_a[1] = (WaitEvent){ e2, EVENT_BIT_B };
    list_b[0] = list_b[1] = (WaitEvent){ e1, EVENT_BIT_A };

    const SceUID a = test_start_thread(waiter_a);
    TEST_WAIT_FOR(in_wait(a, WAIT_TYPE_MP_EVENTS));
    print_counts("after A waits");

    const SceUID b = test_start_thread(waiter_b);
    TEST_WAIT_FOR(in_wait(b, WAIT_TYPE_MP_EVENTS));
    print_counts("after B waits");

    // E2 ends A alone, since A is the only waiter on E2
    PRINT("set E2", sceKernelSetEvent(e2, EVENT_BIT_B, 0));
    test_join_thread(a);
    PRINT("A's wait", result_a);

    // E1 ends B, which is the only waiter left
    PRINT("set E1", sceKernelSetEvent(e1, EVENT_BIT_A, 0));
    test_join_thread(b);
    PRINT("B's wait", result_b);
    print_counts("after both waits end");

    sceKernelDeleteSimpleEvent(e1);
    sceKernelDeleteSimpleEvent(e2);
    test_exit();
    return 0;
}
