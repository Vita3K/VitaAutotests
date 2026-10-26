// What a wait with callbacks leaves in the caller's timeout variable when a callback is already
// pending as the wait is about to sleep, and the callbacks end up giving it what it waits for, for
// the kinds the model cb_wait_early_notify_timeout doesn't cover. The model covers the semaphore,
// the event flag, the simple event and the message pipe receive. These cases cover the mutex, the
// lightweight mutex, the read lock, the write lock, the timer, the condition, the lightweight
// condition, the message pipe send, the thread signal and the thread end.
//
// A thread T creates a callback C, and in the pending cases notifies it before its wait, which only
// leaves it pending since T isn't waiting. Then T waits with callbacks with a timeout of 1 s in a
// variable. In the pending cases C runs twice. Its first run notifies C again, so a callback is
// pending once more when the wait is about to sleep, after its take failed. Its second run gives
// T's object what T waits for, so the take that follows succeeds. Every case has fresh objects and
// runs T alone, the main thread waits for T to end. What T waits for, and what C's second run does
// about it, is the same as in cb_wait_zero_timeout_notify_more: the main thread releases the locks
// and the condition on C's request, and C releases the other objects itself.
//
// Each pending case prints the wait's result, how often C ran, and whether the time left is within
// 250 ms of the timeout passed. The nothing pending cases come first. There T waits, and once the
// main thread sees it waiting, the main thread waits 300 ms and gives T what it waits for. These
// print the wait's result and whether the time left is at least 250 ms below the timeout passed.
//
// The pending lock waits, the message pipe send and the thread end wait are expected to take what
// they wait for at once after the second run, and to return 0 with about the timeout passed. The
// timer wait starts its timer of 400 ms in the second run, so it is expected to sleep until the
// timer fires and to return 0 with about 600 ms left. The condition waits are expected to lose their
// signal, since T has left the condition's queue when the second run signals it, so they sleep and
// time out with the timeout variable at 0. The thread signal case only records what happens: its
// wait may sleep through the callback that notified itself, so how often C runs and what the wait
// returns are printed without a result being assumed. In the nothing pending cases every wait is
// expected to return 0 with about 700 ms left, and the thread signal wait ends by the later send.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define MESSAGE_SIZE 4
#define CAPACITY 4096
#define TIMER_US 400000
#define WAIT_US 1000000
#define NEAR_US 250000
#define LESS_US 250000
#define DELAY_US 300000

typedef enum Kind { MUTEX, LW_MUTEX, READ, WRITE, TIMER, COND, LW_COND, SEND, SIGNAL, END } Kind;

static Kind kind;
static int pending_first;
static SceUID object, mutex, gate, handoff, back, t_thread, helper;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;
static char fill[CAPACITY];
static volatile SceUID callback;
static volatile int callback_runs, wait_result, gave;
static volatile SceUInt time_after;

// Whether the main thread releases what T waits for, since the callback can't do it
static int main_releases(void) {
    return kind == MUTEX || kind == LW_MUTEX || kind == READ || kind == WRITE || kind == COND || kind == LW_COND;
}

// Whether the helper of a thread end case has ended
static int helper_done(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(helper, &info) == 0 && info.status == SCE_THREAD_DORMANT;
}

// Waits for the gate, then returns
static int helper_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// The uid of the lightweight condition, which its wait info reports
static SceUID lw_cond_uid(void) {
    LwCondInfo info = { .size = sizeof(info) };
    return sceKernelGetLwCondInfo(&lw_cond, &info) == 0 ? info.uid : -1;
}

// The uid of the lightweight mutex, which its wait info reports
static SceUID lw_mutex_uid(void) {
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(&lw_mutex, &info) == 0 ? info.uid : -1;
}

// Returns whether thread is in a signal wait with callbacks. A signal wait reports the id of the
// global signal object, so only its type and status are checked.
static int in_signal_wait(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.status == SCE_THREAD_WAITING &&
           info.waitType == (WAIT_TYPE_SIGNAL | WAIT_TYPE_CB);
}

// Returns whether thread waits with callbacks for what the case waits for
static int waits_on_kind(SceUID thread) {
    switch (kind) {
    case MUTEX: return test_in_wait(thread, WAIT_TYPE_MUTEX | WAIT_TYPE_CB, object);
    case LW_MUTEX: return test_in_wait(thread, WAIT_TYPE_LW_MUTEX | WAIT_TYPE_CB, lw_mutex_uid());
    case READ:
    case WRITE: return test_in_wait(thread, WAIT_TYPE_RW_LOCK | WAIT_TYPE_CB, object);
    case TIMER: return test_in_wait(thread, WAIT_TYPE_EVENT | WAIT_TYPE_CB, object);
    case COND: return test_in_wait(thread, WAIT_TYPE_COND | WAIT_TYPE_CB, object);
    case LW_COND: return test_in_wait(thread, WAIT_TYPE_LW_COND | WAIT_TYPE_CB, lw_cond_uid());
    case SEND: return test_in_wait(thread, WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB, object);
    case SIGNAL: return in_signal_wait(thread);
    case END: return test_in_wait(thread, WAIT_TYPE_THREAD_END | WAIT_TYPE_CB, helper);
    }
    return 0;
}

// Gives T's wait what it waits for. The main thread calls it, and so does C for the kinds it can
// release itself.
static void release(void) {
    SceSize size = 0;
    char buffer[MESSAGE_SIZE];
    switch (kind) {
    case MUTEX: sceKernelUnlockMutex(object, 1); break;
    case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
    case READ: sceKernelUnlockWriteRWLock(object); break;
    case WRITE: sceKernelUnlockReadRWLock(object); break;
    case TIMER: sceKernelStartTimer(object); break;
    case COND: sceKernelSignalCond(object); break;
    case LW_COND: sceKernelSignalLwCond(&lw_cond); break;
    // A blocking receive, since a try receive that leaves a sender waiting can hang the console
    case SEND: sceKernelReceiveMsgPipe(object, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, &size, NULL); break;
    case SIGNAL: sceKernelSendSignal(t_thread); break;
    case END: sceKernelSignalSema(gate, 1); break;
    }
}

// The callback asks the main thread to release, and waits until the main thread did
static void hand_off(void) {
    SceUInt timeout = TEST_WAIT_US;
    sceKernelSignalSema(handoff, 1);
    gave = sceKernelWaitSema(back, 1, &timeout) == 0;
}

// The main thread releases for a hand off, if the callback asked for one
static void serve_hand_off(void) {
    SceUInt timeout = TEST_WAIT_US;
    if (sceKernelWaitSema(handoff, 1, &timeout) == 0) {
        release();
        sceKernelSignalSema(back, 1);
    }
}

// The callback's second run gives T's wait what it waits for, by a hand off or by itself
static void give_from_callback(void) {
    if (main_releases()) {
        hand_off();
        return;
    }
    release();
    if (kind == END)
        TEST_WAIT_FOR(helper_done());
    gave = 1;
}

// The first run notifies the callback again, and the second gives T's wait what it waits for
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    if (callback_runs == 1)
        sceKernelNotifyCallback(callback, 1);
    else if (callback_runs == 2)
        give_from_callback();
    return 0;
}

// Unlocks what a wait took, and the mutex a condition wait leaves locked
static void give_back(int result) {
    if (result == 0) {
        switch (kind) {
        case MUTEX: sceKernelUnlockMutex(object, 1); break;
        case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
        case READ: sceKernelUnlockReadRWLock(object); break;
        case WRITE: sceKernelUnlockWriteRWLock(object); break;
        default: break;
        }
    }
    if (kind == COND)
        sceKernelUnlockMutex(mutex, 1);
    if (kind == LW_COND)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
}

// Creates a callback, notifies it if the case says so, and waits with callbacks for 1 s
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    t_thread = sceKernelGetThreadId();
    callback = sceKernelCreateCallback("early", 0, callback_entry, NULL);
    if (pending_first)
        sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = WAIT_US;
    SceUInt32 bits = 0;
    SceUInt64 user_data = 0;
    char buffer[MESSAGE_SIZE] = { 'A', 'B', 'C', 'D' };
    if (kind == COND)
        sceKernelLockMutex(mutex, 1, NULL);
    if (kind == LW_COND)
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    switch (kind) {
    case MUTEX: wait_result = sceKernelLockMutexCB(object, 1, &timeout); break;
    case LW_MUTEX: wait_result = sceKernelLockLwMutexCB(&lw_mutex, 1, &timeout); break;
    case READ: wait_result = sceKernelLockReadRWLockCB(object, &timeout); break;
    case WRITE: wait_result = sceKernelLockWriteRWLockCB(object, &timeout); break;
    case TIMER: wait_result = sceKernelWaitEventCB(object, EVENT_TIMER, &bits, &user_data, &timeout); break;
    case COND: wait_result = sceKernelWaitCondCB(object, &timeout); break;
    case LW_COND: wait_result = sceKernelWaitLwCondCB(&lw_cond, &timeout); break;
    case SEND:
        wait_result = sceKernelSendMsgPipeCB(object, buffer, sizeof(buffer), MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case SIGNAL: wait_result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case END: wait_result = sceKernelWaitThreadEndCB(helper, NULL, &timeout); break;
    }
    time_after = timeout;
    give_back(wait_result);
    sceKernelDeleteCallback(callback);
    return 0;
}

// Creates the object of the case, with the lock held by the main thread where the case needs it
static void setup(void) {
    SceUInt timeout = TEST_WAIT_US;
    SceUInt64 interval = TIMER_US;
    handoff = sceKernelCreateSema("handoff", 0, 0, 1, NULL);
    back = sceKernelCreateSema("back", 0, 0, 1, NULL);
    switch (kind) {
    case MUTEX:
        object = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(object, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case READ:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockWriteRWLock(object, &timeout);
        break;
    case WRITE:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockReadRWLock(object, &timeout);
        break;
    case TIMER:
        object = sceKernelCreateTimer("timer", 0, NULL);
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    case COND:
        mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, mutex, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw cond mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        break;
    case SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
        sceKernelSendMsgPipe(object, fill, CAPACITY, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case SIGNAL: break;
    case END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        helper = test_start_thread(helper_entry);
        break;
    }
}

// Deletes what setup created
static void teardown(void) {
    switch (kind) {
    case MUTEX:
        sceKernelUnlockMutex(object, 1);
        sceKernelDeleteMutex(object);
        break;
    case LW_MUTEX:
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case READ:
    case WRITE:
        sceKernelUnlockWriteRWLock(object);
        sceKernelUnlockReadRWLock(object);
        sceKernelDeleteRWLock(object);
        break;
    case TIMER:
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
        break;
    case COND:
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(mutex);
        break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case SEND: sceKernelDeleteMsgPipe(object); break;
    case SIGNAL: break;
    case END:
        sceKernelSignalSema(gate, 1);
        test_join_thread(helper);
        sceKernelDeleteSema(gate);
        break;
    }
    sceKernelDeleteSema(handoff);
    sceKernelDeleteSema(back);
}

static void run_pending(const char *name, Kind k) {
    kind = k;
    pending_first = 1;
    callback_runs = 0;
    gave = 0;
    wait_result = NOT_DONE;
    time_after = NOT_DONE;
    setup();

    const SceUID t = test_start_thread(t_entry);
    if (main_releases())
        serve_hand_off();
    test_join_thread(t);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(callback_runs, "%s: runs of the callback", name);
    PRINTF(time_after + NEAR_US >= WAIT_US, "%s: the time left is within 250 ms of the timeout passed", name);
    teardown();
}

static void run_nothing_pending(const char *name, Kind k) {
    kind = k;
    pending_first = 0;
    callback_runs = 0;
    gave = 0;
    wait_result = NOT_DONE;
    time_after = NOT_DONE;
    setup();

    const SceUID t = test_start_thread(t_entry);
    TEST_WAIT_FOR(waits_on_kind(t));
    sceKernelDelayThread(DELAY_US);
    release();
    test_join_thread(t);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(time_after + LESS_US <= WAIT_US, "%s: the time left is at least 250 ms below the timeout passed", name);
    teardown();
}

int main(void) {
    run_nothing_pending("mutex, nothing pending", MUTEX);
    run_nothing_pending("lightweight mutex, nothing pending", LW_MUTEX);
    run_nothing_pending("read lock, nothing pending", READ);
    run_nothing_pending("write lock, nothing pending", WRITE);
    run_nothing_pending("timer, nothing pending", TIMER);
    run_nothing_pending("condition, nothing pending", COND);
    run_nothing_pending("lightweight condition, nothing pending", LW_COND);
    run_nothing_pending("message pipe send, nothing pending", SEND);
    run_nothing_pending("thread signal, nothing pending", SIGNAL);
    run_nothing_pending("thread end, nothing pending", END);

    run_pending("mutex, callback pending", MUTEX);
    run_pending("lightweight mutex, callback pending", LW_MUTEX);
    run_pending("read lock, callback pending", READ);
    run_pending("write lock, callback pending", WRITE);
    run_pending("timer, callback pending", TIMER);
    run_pending("condition, callback pending", COND);
    run_pending("lightweight condition, callback pending", LW_COND);
    run_pending("message pipe send, callback pending", SEND);
    run_pending("thread signal, callback pending", SIGNAL);
    run_pending("thread end, callback pending", END);

    test_exit();
    return 0;
}
