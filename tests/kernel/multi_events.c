// Waiting on several events at once, callbacks registered to events, and auto reset events with
// waiters for different bits
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID event;
static int wait_result[2];
static SceUInt32 wait_bits[2] = { EVENT_BIT_A, EVENT_BIT_B };
static int calls;

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 result;
    SceUInt64 user_data;
    SceUInt32 timeout = TEST_WAIT_US;
    wait_result[i] = sceKernelWaitEvent(event, wait_bits[i], &result, &user_data, &timeout);
    return 0;
}

static int event_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    calls++;
    return 0;
}

int main(void) {
    // Two events, one of them set
    const SceUID a = sceKernelCreateSimpleEvent("a", 0, 0, NULL);
    const SceUID b = sceKernelCreateSimpleEvent("b", 0, EVENT_BIT_C, NULL);
    PRINT("create the set event", test_created(b));
    WaitEvent events[2] = { { a, EVENT_BIT_A }, { b, EVENT_BIT_C } };
    ResultEvent results[2] = { { 0 } };
    SceUInt32 timeout = 1000;
    // The results only hold the events whose wait ended, as many as the call returns
    PRINT("wait OR on two events with one set", sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_OR, results, &timeout));
    PRINT("the first result is for the set event", results[0].eventId == b);
    PRINT("its result", results[0].result);
    PRINT("its bits", results[0].resultPattern);
    timeout = 1000;
    PRINT("wait AND on the same two", sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_AND, results, &timeout));
    timeout = 1000;
    PRINT("wait on no events", sceKernelWaitMultipleEvents(events, 0, EVENT_WAIT_MODE_OR, results, &timeout));

    // Whether a callback registered to an event is notified when the event is set
    const SceUID cb = sceKernelCreateCallback("event", 0, event_callback, NULL);
    PRINT("register a callback to an event", sceKernelRegisterCallbackToEvent(a, cb));
    sceKernelSetEvent(a, EVENT_BIT_A, 0);
    PRINT("the callback's notification count after the set", sceKernelGetCallbackCount(cb));
    sceKernelCheckCallback();
    PRINT("its calls after the next check", calls);

    // Auto reset with one waiter for each of two bits, both set at once
    event = sceKernelCreateSimpleEvent("auto", EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID first = test_start_thread_arg(wait_thread, 0);
    test_wait_until_waiting(first);
    const SceUID second = test_start_thread_arg(wait_thread, 1);
    test_wait_until_waiting(second);
    sceKernelSetEvent(event, EVENT_BIT_A | EVENT_BIT_B, 0);
    test_join_thread(first);
    test_join_thread(second);
    PRINT("the waiter for the first bit", wait_result[0]);
    PRINT("the waiter for the second bit", wait_result[1]);
    SceUInt32 pattern = 0xDEAD;
    sceKernelGetEventPattern(event, &pattern);
    PRINT("the bits left after waking both", pattern);

    test_exit();
    return 0;
}
