// Who gets the one unit of an object that a release hands out right after a callback notify, for
// the kinds callback_notify_then_release leaves out. Thread T waits with callbacks at the head of
// the object's FIFO queue and thread W waits, without callbacks, behind it. The main thread
// notifies T's callback, then releases one unit of the object. Does W get the unit, since the
// notify took T out of the queue before the release, or does T? Does T's callback run, and what
// does T's wait return? Every case has a fresh object and fresh threads:
// - a simple event with EVENT_ATTR_AUTO_RESET. T waits for EVENT_BIT_A with sceKernelWaitEventCB, W
//   with sceKernelWaitEvent, and the release sets the bit
// - a timer with EVENT_ATTR_AUTO_RESET, set to fire after 100 ms. T waits for EVENT_TIMER with
//   sceKernelWaitEventCB, W with sceKernelWaitEvent. The release starts the timer, and the second
//   release stops it and starts it again, since a one shot timer that fired only fires again after
//   a stop and a start
// - a message pipe, T receives 4 bytes with sceKernelReceiveMsgPipeCB and W receives them with
//   sceKernelReceiveMsgPipe, and the release sends 4 bytes
// - a message pipe full of data. T sends 4 bytes with sceKernelSendMsgPipeCB and W sends them with
//   sceKernelSendMsgPipe, and the release receives 4 bytes
// - a condition. T locks its mutex and waits in sceKernelWaitCondCB, W locks it and waits in
//   sceKernelWaitCond, and the release is one sceKernelSignalCond
// - a lightweight condition, the same with sceKernelWaitLwCondCB, sceKernelWaitLwCond and
//   sceKernelSignalLwCond
//
// Every thread is pinned to core 0 and the main thread runs at 0x60, above T and W at 0xB0, so
// neither of them runs while the main thread doesn't block. T is started first and the main thread
// waits until it waits, then W is started the same way, which queues T before W. T creates its
// callback itself, which only counts its runs.
//
// The main thread notifies T's callback, reads how many threads the object counts, and releases
// one unit right after, without blocking in between, as callback_notify_then_release does. Then the
// main thread waits until W's wait ended, which is when W got the unit, and until T waits again after its
// callback. It reads the object's count then, releases a second unit, and waits for both threads to
// end, so nothing is read while another thread changes state. Every wait has a timeout of 6
// seconds, so a case can't hang. Nothing is printed until both threads ended.
//
// Each case prints these values:
// - whether T and W both wait, T first, before the notify
// - the result of the notify, the threads the object counts right after it, and the release
// - who got the unit: 1 if W's wait ended while T still waited, 2 if T's wait ended first, 0 if
//   neither did
// - whether T waits again after its callback, the object's count of waiting threads then, and the
//   result of the second release
// - how often T's callback ran when T's wait returned, the result of the sceKernelCheckCallback
//   that T calls after its wait, and how often the callback ran after that
// - the results of T's wait and of W's wait
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define THREAD_PRIORITY 0xB0
#define WAIT_TIMEOUT (3 * TEST_WAIT_US)
#define NOT_DONE 0xDEAD
#define MSG_SIZE 4
#define PIPE_SIZE 4096
#define TIMER_INTERVAL_US 100000

typedef enum Kind { SIMPLE_EVENT, TIMER, PIPE_RECEIVE, PIPE_SEND, COND, LW_COND } Kind;

static Kind kind;
static SceUID object, cond_mutex, t_thread, w_thread;
static SceKernelLwMutexWork lw_cond_mutex;
static SceKernelLwCondWork lw_cond;
static char t_buf[MSG_SIZE], w_buf[MSG_SIZE] = { 'w', 'x', 'y', 'z' };
static char main_buf[MSG_SIZE] = { 'a', 'b', 'c', 'd' }, fill[PIPE_SIZE];
static volatile SceUID callback_id;
static volatile int callback_runs, t_done, w_done;
static volatile int t_result, w_result, runs_at_return, check_result, runs_after_check;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Returns the UID that a wait on the lightweight condition reports
static SceUID lw_cond_uid(SceKernelLwCondWork *work) {
    LwCondInfo info = { .size = sizeof(info) };
    return sceKernelGetLwCondInfo(work, &info) == 0 ? info.uid : -1;
}

// Returns the wait type of the object of the case, without the callback bit
static SceUInt32 wait_type(void) {
    switch (kind) {
    case SIMPLE_EVENT:
    case TIMER: return WAIT_TYPE_EVENT;
    case PIPE_RECEIVE:
    case PIPE_SEND: return WAIT_TYPE_MSG_PIPE;
    case COND: return WAIT_TYPE_COND;
    case LW_COND: return WAIT_TYPE_LW_COND;
    }
    return 0;
}

// Returns whether T waits with callbacks on the object of the case
static int t_waits(void) {
    return test_in_wait(t_thread, wait_type() | WAIT_TYPE_CB, object);
}

// Returns whether W waits on the object of the case
static int w_waits(void) {
    return test_in_wait(w_thread, wait_type(), object);
}

// Returns how many threads the object counts as waiting for the wait type of the case, or NOT_DONE
static int waiters(void) {
    switch (kind) {
    case SIMPLE_EVENT:
    case TIMER: {
        EventInfo info = { .size = sizeof(info) };
        return sceKernelGetEventInfo(object, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
    }
    case PIPE_RECEIVE: {
        SceKernelMppInfo info = { .size = sizeof(info) };
        return sceKernelGetMsgPipeInfo(object, &info) == 0 ? info.numReceiveWaitThreads : NOT_DONE;
    }
    case PIPE_SEND: {
        SceKernelMppInfo info = { .size = sizeof(info) };
        return sceKernelGetMsgPipeInfo(object, &info) == 0 ? info.numSendWaitThreads : NOT_DONE;
    }
    case COND: {
        SceKernelCondInfo info = { .size = sizeof(info) };
        return sceKernelGetCondInfo(object, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
    }
    case LW_COND: {
        LwCondInfo info = { .size = sizeof(info) };
        return sceKernelGetLwCondInfo(&lw_cond, &info) == 0 ? (int)info.numWaitThreads : NOT_DONE;
    }
    }
    return NOT_DONE;
}

// Creates the object of the case, filled for a message pipe send
static void create_object(void) {
    SceUInt timeout = WAIT_TIMEOUT;
    switch (kind) {
    case SIMPLE_EVENT: object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL); break;
    case TIMER: {
        object = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
        SceUInt64 interval = TIMER_INTERVAL_US;
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    }
    case PIPE_RECEIVE: object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL); break;
    case PIPE_SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
        sceKernelSendMsgPipe(object, fill, sizeof(fill), MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case COND:
        cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_cond_mutex, "lw cond mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_cond_mutex, NULL);
        object = lw_cond_uid(&lw_cond);
        break;
    }
}

static void delete_object(void) {
    switch (kind) {
    case SIMPLE_EVENT: sceKernelDeleteSimpleEvent(object); break;
    case TIMER: sceKernelDeleteTimer(object); break;
    case PIPE_RECEIVE:
    case PIPE_SEND: sceKernelDeleteMsgPipe(object); break;
    case COND:
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(cond_mutex);
        break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_cond_mutex);
        break;
    }
}

// Locks the mutex of a condition wait, which the wait releases while it sleeps
static void lock_for_wait(void) {
    if (kind == COND)
        sceKernelLockMutex(cond_mutex, 1, NULL);
    if (kind == LW_COND)
        sceKernelLockLwMutex(&lw_cond_mutex, 1, NULL);
}

static void unlock_after_wait(void) {
    if (kind == COND)
        sceKernelUnlockMutex(cond_mutex, 1);
    if (kind == LW_COND)
        sceKernelUnlockLwMutex(&lw_cond_mutex, 1);
}

// Gives the object one unit. The second release of a timer stops it first, since a one shot timer
// that fired only fires again after a stop and a start.
static int release(int second) {
    SceUInt timeout = WAIT_TIMEOUT;
    switch (kind) {
    case SIMPLE_EVENT: return sceKernelSetEvent(object, EVENT_BIT_A, 0);
    case TIMER:
        if (second)
            sceKernelStopTimer(object);
        return sceKernelStartTimer(object);
    case PIPE_RECEIVE: return sceKernelSendMsgPipe(object, main_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout);
    case PIPE_SEND: return sceKernelReceiveMsgPipe(object, main_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout);
    case COND: return sceKernelSignalCond(object);
    case LW_COND: return sceKernelSignalLwCond(&lw_cond);
    }
    return NOT_DONE;
}

// Waits with callbacks in the kind of the case, and reads the results right after the wait
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback_id = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    SceUInt timeout = WAIT_TIMEOUT;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    lock_for_wait();
    switch (kind) {
    case SIMPLE_EVENT: t_result = sceKernelWaitEventCB(object, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    case TIMER:
        t_result = sceKernelWaitEventCB(object, EVENT_TIMER, &pattern, &user_data, &timeout);
        break;
    case PIPE_RECEIVE:
        t_result = sceKernelReceiveMsgPipeCB(object, t_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case PIPE_SEND: t_result = sceKernelSendMsgPipeCB(object, t_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case COND: t_result = sceKernelWaitCondCB(object, &timeout); break;
    case LW_COND: t_result = sceKernelWaitLwCondCB(&lw_cond, &timeout); break;
    }
    unlock_after_wait();
    runs_at_return = callback_runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    sceKernelDeleteCallback(callback_id);
    t_done = 1;
    return 0;
}

// Waits without callbacks in the kind of the case
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_TIMEOUT;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    lock_for_wait();
    switch (kind) {
    case SIMPLE_EVENT: w_result = sceKernelWaitEvent(object, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    case TIMER:
        w_result = sceKernelWaitEvent(object, EVENT_TIMER, &pattern, &user_data, &timeout);
        break;
    case PIPE_RECEIVE: w_result = sceKernelReceiveMsgPipe(object, w_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case PIPE_SEND: w_result = sceKernelSendMsgPipe(object, w_buf, MSG_SIZE, MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case COND: w_result = sceKernelWaitCond(object, &timeout); break;
    case LW_COND: w_result = sceKernelWaitLwCond(&lw_cond, &timeout); break;
    }
    unlock_after_wait();
    w_done = 1;
    return 0;
}

// Starts a thread on core 0 below the main thread
static SceUID start(const char *name, SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread(name, entry, THREAD_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static void run_case(const char *name, Kind k) {
    kind = k;
    callback_id = 0;
    callback_runs = t_done = w_done = 0;
    t_result = w_result = runs_at_return = check_result = runs_after_check = NOT_DONE;
    create_object();

    t_thread = start("t", t_entry);
    TEST_WAIT_FOR(callback_id && t_waits());
    const int t_waits_first = callback_id && t_waits();
    w_thread = start("w", w_entry);
    TEST_WAIT_FOR(w_waits());
    const int both_wait = t_waits_first && w_waits();

    const int notify = sceKernelNotifyCallback(callback_id, 1);
    const int after_notify = waiters();
    const int release_result = release(0);
    TEST_WAIT_FOR(w_done);
    const int got = w_done && !t_done ? 1 : t_done ? 2 : 0;

    TEST_WAIT_FOR(t_done || (callback_runs > 0 && t_waits() && waiters() == 1));
    const int again = !t_done && callback_runs > 0 && t_waits();
    const int again_waiters = waiters();
    const int second = release(1);
    TEST_WAIT_FOR(t_done && w_done);
    test_join_thread(t_thread);
    test_join_thread(w_thread);
    delete_object();

    PRINTF(both_wait, "%s: T then W wait before the notify", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(after_notify, "%s: the waiting threads right after the notify", name);
    PRINTF(release_result, "%s: the release", name);
    PRINTF(got, "%s: who got the unit", name);
    PRINTF(again, "%s: T waits again after its callback", name);
    PRINTF(again_waiters, "%s: the waiting threads when T waits again", name);
    PRINTF(second, "%s: the second release", name);
    PRINTF(runs_at_return, "%s: runs of T's callback when its wait returned", name);
    PRINTF(check_result, "%s: the check of T's callbacks after its wait", name);
    PRINTF(runs_after_check, "%s: runs of T's callback after the check", name);
    PRINTF(t_result, "%s: T's wait", name);
    PRINTF(w_result, "%s: W's wait", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("simple event", SIMPLE_EVENT);
    run_case("timer", TIMER);
    run_case("message pipe receive", PIPE_RECEIVE);
    run_case("message pipe send", PIPE_SEND);
    run_case("condition", COND);
    run_case("lightweight condition", LW_COND);

    test_exit();
    return 0;
}
