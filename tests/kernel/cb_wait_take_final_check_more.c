// Whether a wait with callbacks that takes what it waits for at once runs a callback that was
// notified during its callback run before it returns, for the kinds cb_wait_take_final_check leaves
// out. The model covers the semaphore, event flag, simple event, mutex, rwlock and message pipe
// receive and send waits. Every case has fresh objects. The main thread creates a callback C and
// notifies it, which only leaves it pending since the main thread isn't waiting, and then makes the
// wait with callbacks on an object that already holds what it waits for, so C runs at the start of
// the wait. Its first run notifies C again, and its later runs do nothing. The waits:
// - lightweight mutex: sceKernelLockLwMutexCB of a free lightweight mutex
// - timer: sceKernelWaitEventCB for EVENT_TIMER on a timer whose event has fired and stays set
// - condition: sceKernelWaitCondCB with the main thread holding the mutex. A condition wait has no
//   take at once, since only a signal ends it, so a helper thread signals once the main thread is
//   in the wait. C is pending when the wait starts, so its first run's notify makes the wait loop
//   once more before it sleeps
// - lightweight condition: the same with sceKernelWaitLwCondCB and a lightweight mutex
// - thread signal: sceKernelWaitSignalCB, with a signal the main thread sent itself before
// - thread end: sceKernelWaitThreadEndCB of a thread that has already ended
// - delay: sceKernelDelayThreadCB for 2 s, which the first run's notify doesn't cut short
// - multiple events: sceKernelWaitMultipleEventsCB on a simple event that holds its bit. C is
//   pending before the call only and is not notified again, since a notify while a thread waits in
//   this call hangs the console
// Each case prints the wait's result and how often C ran when the wait returned, then calls
// sceKernelCheckCallback and prints its result and how often C ran after it.
//
// On a PS Vita TV on firmware 3.65 the lightweight mutex takes a free mutex in its user mode path,
// so the wait returns 0 without running C, and the check runs C once and returns 1. The timer,
// thread signal and thread end waits return 0 with C run once and pending again, and the check runs
// it and returns 1. The delay returns 0 at once, since its first pass ends without a sleep when C
// is pending again, so C is run once and pending again. The condition and lightweight condition
// waits return 0 with C run twice, because the wait loops once more before it sleeps, and the check
// returns 0 since nothing is pending. The multiple events wait returns 0 with C run once, and the
// check returns 0 and leaves the count as it is, since C isn't notified again.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define DELAY_US 2000000
#define TIMER_INTERVAL_US 20000

typedef enum Kind { LW_MUTEX, TIMER, COND, LW_COND, SIGNAL, END, DELAY, MULTI } Kind;

static Kind kind;
static SceUID main_thread, callback, object, mutex, helper, signaler;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static SceUInt64 interval = TIMER_INTERVAL_US;
static volatile int callback_runs;
// Whether the first run notifies the callback again
static int renotify;

// The first run notifies the callback again, the later ones do nothing
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (callback_runs == 1 && renotify)
        sceKernelNotifyCallback(callback, 1);
    return 0;
}

// A thread that ends at once
static int exit_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Whether the timer's event has fired, which a poll of the manual reset event shows without
// taking it
static int timer_fired(void) {
    SceUInt32 result;
    SceUInt64 user_data;
    return sceKernelPollEvent(object, EVENT_TIMER, &result, &user_data) == 0;
}

// Whether the main thread waits on the condition, with callbacks
static int main_in_cond_wait(void) {
    if (kind == COND)
        return test_in_wait(main_thread, WAIT_TYPE_COND | WAIT_TYPE_CB, object);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(main_thread, &info) == 0 && info.waitType == (WAIT_TYPE_LW_COND | WAIT_TYPE_CB);
}

// Signals the condition once the main thread waits on it
static int signaler_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    TEST_WAIT_FOR(main_in_cond_wait());
    if (kind == COND)
        sceKernelSignalCond(object);
    else
        sceKernelSignalLwCond(&lw_cond);
    return 0;
}

// Creates what the wait of the case needs, and the state it starts in
static void setup(Kind k) {
    switch (k) {
    case LW_MUTEX: sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL); break;
    case TIMER:
        object = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        sceKernelStartTimer(object);
        TEST_WAIT_FOR(timer_fired());
        break;
    case COND:
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, mutex, NULL);
        sceKernelLockMutex(mutex, 1, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case END:
        helper = test_start_thread(exit_entry);
        sceKernelWaitThreadEnd(helper, NULL, NULL);
        break;
    case MULTI:
        object = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
        sceKernelSetEvent(object, EVENT_BIT_A, 0);
        break;
    case SIGNAL:
    case DELAY: break;
    }
}

// Makes the wait of the case with callbacks, and returns its result
static int wait_call(SceUInt *timeout) {
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    WaitEvent events = { object, EVENT_BIT_A };
    ResultEvent results;
    switch (kind) {
    case LW_MUTEX: return sceKernelLockLwMutexCB(&lw_mutex, 1, timeout);
    case TIMER: return sceKernelWaitEventCB(object, EVENT_TIMER, &bits, &user_data, timeout);
    case COND: return sceKernelWaitCondCB(object, timeout);
    case LW_COND: return sceKernelWaitLwCondCB(&lw_cond, timeout);
    case SIGNAL: return sceKernelWaitSignalCB(0, 0, timeout);
    case END: return sceKernelWaitThreadEndCB(helper, NULL, timeout);
    case DELAY: return sceKernelDelayThreadCB(DELAY_US);
    case MULTI: return sceKernelWaitMultipleEventsCB(&events, 1, EVENT_WAIT_MODE_OR, &results, timeout);
    }
    return NOT_DONE;
}

// Deletes what setup created, and gives back what the wait took
static void cleanup(Kind k) {
    switch (k) {
    case LW_MUTEX:
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case TIMER: sceKernelDeleteTimer(object); break;
    case COND:
        sceKernelUnlockMutex(mutex, 1);
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(mutex);
        break;
    case LW_COND:
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case END: test_join_thread(helper); break;
    case MULTI: sceKernelDeleteSimpleEvent(object); break;
    case SIGNAL:
    case DELAY: break;
    }
}

static void run_case(const char *name, Kind k) {
    kind = k;
    renotify = k != MULTI;
    callback_runs = 0;
    signaler = -1;
    setup(k);

    callback = sceKernelCreateCallback("again", 0, callback_entry, NULL);
    sceKernelNotifyCallback(callback, 1);
    if (k == COND || k == LW_COND)
        signaler = test_start_thread(signaler_entry);
    if (k == SIGNAL)
        sceKernelSendSignal(main_thread);
    SceUInt timeout = TEST_WAIT_US;
    const int result = wait_call(&timeout);
    const int runs_in_wait = callback_runs;
    const int check = sceKernelCheckCallback();
    const int runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback);
    if (signaler >= 0)
        test_join_thread(signaler);
    cleanup(k);

    PRINTF(result, "%s: the wait", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(check, "%s: the check", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
}

int main(void) {
    main_thread = sceKernelGetThreadId();

    run_case("lightweight mutex", LW_MUTEX);
    run_case("timer", TIMER);
    run_case("condition", COND);
    run_case("lightweight condition", LW_COND);
    run_case("thread signal", SIGNAL);
    run_case("thread end", END);
    run_case("delay", DELAY);
    run_case("multiple events", MULTI);

    test_exit();
    return 0;
}
