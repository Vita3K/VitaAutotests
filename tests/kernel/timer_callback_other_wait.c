// What a timer's fire does to a callback registered to the timer, when the thread that registered
// it waits with callbacks on another object, and when it waits on the timer itself. The waiter T
// creates its callback, registers it to the timer and waits with a timeout of 8 s. Once T waits,
// the main thread starts a one shot set event of 300 ms, and its fire notifies the callback. The
// callback only records each run: the notify ID, count and argument it got, and whether it ran on
// T. The main thread is pinned to core 1 and T to core 0. Cases:
// - a semaphore at 0, T in sceKernelWaitSemaCB
// - an event flag, T in sceKernelWaitEventFlagCB for a bit not set
// - a simple event, T in sceKernelWaitEventCB for a bit not set
// - a semaphore at 0, T in sceKernelWaitSema, a wait without callbacks, as a reference
// - the timer itself, T in sceKernelWaitEventCB for the timer's bit
// In the first three, the main thread waits until the callback ran and T waits again, then reads
// the count of threads waiting on the object and T's wait type, ends T's wait with the call that
// releases the object, and joins T. In the reference, the main thread waits until the callback has
// a pending notify, then reads the count and releases the semaphore. T counts the runs of the
// callback before and after its sceKernelCheckCallback. For the timer, T's wait returns when the
// timer fires. Every case prints after its waits have ended.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define TIMER_US 300000
// Longer than the 5 s the gates poll for, so T can't time out before the main thread ends it
#define WAIT_US (4 * TEST_WAIT_US)
#define LONG_ENOUGH_US 4000000

enum Kind { SEMA, EVENT_FLAG, SIMPLE_EVENT, PLAIN_SEMA, TIMER_EVENT };

typedef struct Case {
    const char *name;
    enum Kind kind;
    // The wait type of T's wait without WAIT_TYPE_CB
    SceUInt32 wait_type;
} Case;

static SceUID timer, object, notify_cb, waiter_thread;
static enum Kind kind;
static volatile int register_result, start_result, wait_result, callback_runs, ran_on_waiter;
static volatile int got_notify_id, got_count, got_arg, runs_at_return;
static volatile int runs_before_check, runs_after_check;
static volatile int time_left;

// Records each run of the callback: the notify ID, count and argument, and whether it ran on T
static int recording_callback(int notify_id, int count, int arg, void *common) {
    (void)common;
    callback_runs++;
    got_notify_id = notify_id;
    got_count = count;
    got_arg = arg;
    ran_on_waiter = sceKernelGetThreadId() == waiter_thread;
    return 0;
}

// Creates its callback, registers it to the timer, then waits on the case's object or the timer
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    waiter_thread = sceKernelGetThreadId();
    notify_cb = sceKernelCreateCallback("notified", 0, recording_callback, NULL);
    register_result = sceKernelRegisterCallbackToEvent(timer, notify_cb);
    SceUInt timeout = WAIT_US;
    SceUInt32 bits = 0;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    switch (kind) {
    case SEMA: wait_result = sceKernelWaitSemaCB(object, 1, &timeout); break;
    case EVENT_FLAG: wait_result = sceKernelWaitEventFlagCB(object, 0x1, SCE_EVENT_WAITOR, &bits, &timeout); break;
    case SIMPLE_EVENT: wait_result = sceKernelWaitEventCB(object, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    case PLAIN_SEMA: wait_result = sceKernelWaitSema(object, 1, &timeout); break;
    case TIMER_EVENT: wait_result = sceKernelWaitEventCB(timer, EVENT_TIMER, &pattern, &user_data, &timeout); break;
    }
    time_left = (int)timeout;
    runs_at_return = callback_runs;
    if (kind == PLAIN_SEMA) {
        runs_before_check = callback_runs;
        sceKernelCheckCallback();
        runs_after_check = callback_runs;
    }
    sceKernelUnregisterCallbackFromEvent(timer, notify_cb);
    sceKernelDeleteCallback(notify_cb);
    return 0;
}

// Starts entry on a new thread pinned to core 0, where the main thread isn't
static SceUID start_waiter(SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread(
        "waiter", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Creates the object the case waits on. The timer case waits on the timer, which is created first.
static void create_object(enum Kind k) {
    switch (k) {
    case SEMA:
    case PLAIN_SEMA: object = sceKernelCreateSema("sema", 0, 0, 1, NULL); break;
    case EVENT_FLAG: object = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case SIMPLE_EVENT: object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL); break;
    case TIMER_EVENT: object = timer; break;
    }
}

static void delete_object(enum Kind k) {
    switch (k) {
    case SEMA:
    case PLAIN_SEMA: sceKernelDeleteSema(object); break;
    case EVENT_FLAG: sceKernelDeleteEventFlag(object); break;
    case SIMPLE_EVENT: sceKernelDeleteSimpleEvent(object); break;
    case TIMER_EVENT: break;
    }
}

// Ends T's wait on the object by the call that releases it, and returns that call's result
static int end_wait(enum Kind k) {
    switch (k) {
    case SEMA:
    case PLAIN_SEMA: return sceKernelSignalSema(object, 1);
    case EVENT_FLAG: return sceKernelSetEventFlag(object, 0x1);
    case SIMPLE_EVENT: return sceKernelSetEvent(object, EVENT_BIT_A, 0);
    case TIMER_EVENT: break;
    }
    return NOT_DONE;
}

// Returns the number of threads waiting on the object, or NOT_DONE if its info call fails
static int waiting_threads(enum Kind k) {
    int threads = NOT_DONE;
    switch (k) {
    case SEMA:
    case PLAIN_SEMA: {
        SceKernelSemaInfo info = { .size = sizeof(info) };
        if (sceKernelGetSemaInfo(object, &info) == 0)
            threads = info.numWaitThreads;
        break;
    }
    case EVENT_FLAG: {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        if (sceKernelGetEventFlagInfo(object, &info) == 0)
            threads = info.numWaitThreads;
        break;
    }
    case SIMPLE_EVENT: {
        EventInfo info = { .size = sizeof(info) };
        if (sceKernelGetEventInfo(object, &info) == 0)
            threads = info.numWaitThreads;
        break;
    }
    case TIMER_EVENT: break;
    }
    return threads;
}

// Returns the wait type of thread, or NOT_DONE if its info call fails
static int wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    int type = NOT_DONE;
    if (sceKernelGetThreadInfo(thread, &info) == 0)
        type = (int)info.waitType;
    return type;
}

static void run_case(const Case *c) {
    const int with_callbacks = c->kind != PLAIN_SEMA;
    const SceUInt32 type = c->wait_type | (with_callbacks ? WAIT_TYPE_CB : 0);
    kind = c->kind;
    register_result = start_result = wait_result = NOT_DONE;
    callback_runs = 0;
    ran_on_waiter = 0;
    got_notify_id = got_count = got_arg = NOT_DONE;
    runs_at_return = runs_before_check = runs_after_check = NOT_DONE;
    time_left = NOT_DONE;

    timer = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET | EVENT_ATTR_NOTIFY_CB_ALL, NULL);
    SceUInt64 interval = TIMER_US;
    sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
    create_object(kind);
    const SceUID thread = start_waiter(waiter);
    TEST_WAIT_FOR(test_in_wait(thread, type, object));
    start_result = sceKernelStartTimer(timer);

    int threads = NOT_DONE;
    int type_after = NOT_DONE;
    int pending = NOT_DONE;
    int end = NOT_DONE;
    if (kind == PLAIN_SEMA) {
        TEST_WAIT_FOR(sceKernelGetCallbackCount(notify_cb) > 0);
        pending = sceKernelGetCallbackCount(notify_cb);
        threads = waiting_threads(kind);
        end = end_wait(kind);
    } else if (kind != TIMER_EVENT) {
        TEST_WAIT_FOR(callback_runs > 0 && test_in_wait(thread, type, object));
        threads = waiting_threads(kind);
        type_after = wait_type_of(thread);
        end = end_wait(kind);
    }
    test_join_thread(thread);

    PRINTF(register_result, "%s: the callback's register", c->name);
    PRINTF(start_result, "%s: the timer start", c->name);
    if (kind == TIMER_EVENT) {
        PRINTF(wait_result, "%s: T's wait result", c->name);
        PRINTF(runs_at_return, "%s: runs of the callback when T's wait returned", c->name);
    } else if (kind == PLAIN_SEMA) {
        PRINTF(threads, "%s: the waiting threads before the signal", c->name);
        PRINTF(pending, "%s: the notifies pending before the signal", c->name);
        PRINTF(end, "%s: the signal", c->name);
        PRINTF(runs_before_check, "%s: runs of the callback before T's check", c->name);
        PRINTF(runs_after_check, "%s: runs of the callback after T's check", c->name);
        PRINTF(wait_result, "%s: T's wait result", c->name);
    } else {
        PRINTF(callback_runs, "%s: runs of the callback once T waits again", c->name);
        PRINTF(ran_on_waiter, "%s: the callback ran on T", c->name);
        PRINTF(got_notify_id == timer, "%s: the notify ID is the timer", c->name);
        PRINTF(got_count, "%s: the notify count", c->name);
        PRINTF(got_arg, "%s: the notify argument", c->name);
        PRINTF(threads, "%s: the waiting threads after T waits again", c->name);
        PRINTF(type_after, "%s: T's wait type after T waits again", c->name);
        PRINTF(end, "%s: the call that ends T's wait", c->name);
        PRINTF(wait_result, "%s: T's wait result", c->name);
        PRINTF(time_left > LONG_ENOUGH_US, "%s: T's time left is over 4 s", c->name);
        PRINTF(time_left < WAIT_US, "%s: T's time left is under the timeout", c->name);
    }

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
    delete_object(kind);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    static const Case cases[] = {
        { "semaphore", SEMA, WAIT_TYPE_SEMA },
        { "event flag", EVENT_FLAG, WAIT_TYPE_EVENT_FLAG },
        { "simple event", SIMPLE_EVENT, WAIT_TYPE_EVENT },
        { "semaphore without callbacks", PLAIN_SEMA, WAIT_TYPE_SEMA },
        { "timer itself", TIMER_EVENT, WAIT_TYPE_EVENT },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    test_exit();
    return 0;
}
