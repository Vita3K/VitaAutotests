// The time left a wait writes back to its timeout, for the kinds that the model timeout_left_kinds
// leaves out: a semaphore, a timer, and a wait on a timer and a simple event where the timer fires
// before the timeout. Each wait runs on a thread of its own, on fresh objects, as in the model.
// First each wait is woken soon after it starts waiting, with a timeout of 10 s: the semaphore is
// signaled, and the timers start and fire 500 ms later. The test prints the result and whether the
// time left is less than the 10 s and more than half of it. Then the semaphore and the timer that
// never starts are left to time out after 10 ms, and the test prints the result and whether the
// time left is 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LONG 10000000
#define SHORT 10000
// The timer fires this long after it starts, far inside the timeout of LONG
#define FIRE_US 500000

enum Kind { SEMA, TIMER, MULTI_TIMER, KIND_COUNT };

static const char *const kind_names[] = {
    "semaphore",
    "timer",
    "multiple events, one of them a timer",
};

static volatile int kind, wait_result;
static volatile SceUInt left;
static SceUInt given;
static SceUID object, other;

// Whether the thread is in a wait
static int waiting(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType != 0;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = given;
    SceUInt32 bits;
    SceUInt64 user_data;
    WaitEvent events[2] = { { object, EVENT_TIMER }, { other, EVENT_BIT_A } };
    ResultEvent results[2];
    int result = -1;
    switch (kind) {
    case SEMA: result = sceKernelWaitSema(object, 1, &timeout); break;
    case TIMER: result = sceKernelWaitEvent(object, EVENT_TIMER, &bits, &user_data, &timeout); break;
    case MULTI_TIMER: result = sceKernelWaitMultipleEvents(events, 2, EVENT_WAIT_MODE_OR, results, &timeout); break;
    }
    wait_result = result;
    left = timeout;
    return 0;
}

// Sets the timer's event to fire once, FIRE_US after the timer starts
static void set_fire(void) {
    SceUInt64 interval = FIRE_US;
    sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
}

// Makes the objects for the wait, empty so that it blocks
static void make(void) {
    switch (kind) {
    case SEMA: object = sceKernelCreateSema("left", 0, 0, 1, NULL); break;
    case TIMER:
        object = sceKernelCreateTimer("left", EVENT_ATTR_AUTO_RESET, NULL);
        set_fire();
        break;
    case MULTI_TIMER:
        object = sceKernelCreateTimer("left", EVENT_ATTR_AUTO_RESET, NULL);
        set_fire();
        other = sceKernelCreateSimpleEvent("left other", 0, 0, NULL);
        break;
    }
}

// Ends the wait as the object allows: a signal, or starting the timer
static void wake(void) {
    switch (kind) {
    case SEMA: sceKernelSignalSema(object, 1); break;
    case TIMER:
    case MULTI_TIMER: sceKernelStartTimer(object); break;
    }
}

// Deletes the objects of the wait
static void destroy(void) {
    switch (kind) {
    case SEMA: sceKernelDeleteSema(object); break;
    case TIMER: sceKernelDeleteTimer(object); break;
    case MULTI_TIMER:
        sceKernelDeleteTimer(object);
        sceKernelDeleteSimpleEvent(other);
        break;
    }
}

static void woken(int which) {
    kind = which;
    given = LONG;
    make();
    const SceUID thread = test_start_thread(waiter);
    TEST_WAIT_FOR(waiting(thread));
    wake();
    test_join_thread(thread);
    destroy();
    const char *name = kind_names[which];
    PRINTF(wait_result, "%s, woken: the wait", name);
    PRINTF(left < LONG, "%s, woken: the time left is less than the timeout", name);
    PRINTF(left > LONG / 2, "%s, woken: the time left is more than half of it", name);
}

static void timed_out(int which) {
    kind = which;
    given = SHORT;
    make();
    const SceUID thread = test_start_thread(waiter);
    test_join_thread(thread);
    destroy();
    const char *name = kind_names[which];
    PRINTF(wait_result, "%s, timed out: the wait", name);
    PRINTF(left == 0, "%s, timed out: the time left is 0", name);
}

int main(void) {
    static const int woken_kinds[] = { SEMA, TIMER, MULTI_TIMER };
    for (unsigned int i = 0; i < sizeof(woken_kinds) / sizeof(woken_kinds[0]); i++)
        woken(woken_kinds[i]);
    static const int timing_out[] = { SEMA, TIMER };
    for (unsigned int i = 0; i < sizeof(timing_out) / sizeof(timing_out[0]); i++)
        timed_out(timing_out[i]);
    test_exit();
    return 0;
}
