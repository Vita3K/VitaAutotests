// Thread signals sent to a thread that waits on an object don't end that wait. For each kind of
// wait, the waiter W waits with a timeout of 2 seconds. Once W is waiting, the main thread sends W
// a signal and waits 100 ms. Then, while W is still blocked, it reads whether W still waits and how
// many threads wait on the object, where the object counts them. Then it releases the object, and
// W's wait returns 0 with most of its timeout left. W then calls sceKernelWaitSignal with a timeout
// of 1 second, which returns 0 at once, since the signal stayed pending. The kinds, and how each is
// released:
// - a semaphore, sceKernelWaitSema, released by sceKernelSignalSema
// - an event flag, sceKernelWaitEventFlag for bit 0x1, released by sceKernelSetEventFlag
// - a mutex the main thread holds, sceKernelLockMutex, released by sceKernelUnlockMutex
// - a lightweight mutex the main thread holds, sceKernelLockLwMutex, released by
//   sceKernelUnlockLwMutex
// - a write lock the main thread holds, sceKernelLockWriteRWLock, released by
//   sceKernelUnlockWriteRWLock
// - a simple event, sceKernelWaitEvent for bit 0x01000000, released by sceKernelSetEvent
// - a timer, sceKernelWaitEvent for EVENT_TIMER, released by setting the timer's event to fire
//   after 50 ms and starting the timer
// - a message pipe, sceKernelReceiveMsgPipe of 4 bytes, released by sending 4 bytes
// - a condition variable, sceKernelWaitCond while W holds its mutex, released by
//   sceKernelSignalCond
// - a lightweight condition variable, sceKernelWaitLwCond while W holds its lightweight mutex,
//   released by sceKernelSignalLwCond
// - the end of a thread, sceKernelWaitThreadEnd, released by letting that thread return
// - a delay, sceKernelDelayThread for 1 s, which the signal doesn't cut short. Nothing is
//   released, and the check is that the delay lasted at least 500 ms.
//
// Every thread of a case runs at the default priority. Each case has fresh objects and threads.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind {
    SEMA,
    EVENT_FLAG,
    MUTEX,
    LW_MUTEX,
    WRITE_LOCK,
    SIMPLE_EVENT,
    TIMER,
    RECEIVE,
    COND,
    LW_COND,
    DELAY,
    THREAD_END,
    KIND_COUNT
};

static const char *const kind_names[] = {
    "semaphore", "event flag",           "mutex",     "lightweight mutex",     "write lock", "simple event",
    "timer",     "message pipe receive", "condition", "lightweight condition", "delay",      "thread end",
};

#define DELAY_US 1000000
#define SIGNAL_WAIT_US 1000000
#define AT_ONCE_US 500000
#define TIMER_INTERVAL_US 50000

static SceUID object, cond_mutex, gate, target;
static SceKernelLwMutexWork lw_mutex;
static SceKernelLwCondWork lw_cond;

// What the waiter records, read by the main thread once the waiter has ended
static volatile int w_done, w_result, w_sig_result, w_sig_at_once;
static volatile SceUInt32 w_left;
static volatile SceUInt64 w_elapsed;

static int thread_is_waiting(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType != 0;
}

// Returns when the gate is signaled, or after a timeout long enough that the case ends first
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Makes the wait of kind k with the given timeout, and gives back any lock it took
static int wait_on(int k, SceUInt32 *timeout) {
    unsigned int bits = 0;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    SceSize received = 0;
    char buf[4];
    int result = 0;
    switch (k) {
    case SEMA:
        return sceKernelWaitSema(object, 1, timeout);
    case EVENT_FLAG:
        return sceKernelWaitEventFlag(object, 0x1, SCE_EVENT_WAITAND, &bits, timeout);
    case MUTEX:
        result = sceKernelLockMutex(object, 1, timeout);
        if (result == 0)
            sceKernelUnlockMutex(object, 1);
        return result;
    case LW_MUTEX:
        result = sceKernelLockLwMutex(&lw_mutex, 1, timeout);
        if (result == 0)
            sceKernelUnlockLwMutex(&lw_mutex, 1);
        return result;
    case WRITE_LOCK:
        result = sceKernelLockWriteRWLock(object, timeout);
        if (result == 0)
            sceKernelUnlockWriteRWLock(object);
        return result;
    case SIMPLE_EVENT:
        return sceKernelWaitEvent(object, EVENT_BIT_A, &pattern, &user_data, timeout);
    case TIMER:
        return sceKernelWaitEvent(object, EVENT_TIMER, &pattern, &user_data, timeout);
    case RECEIVE:
        return sceKernelReceiveMsgPipe(object, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &received, timeout);
    case COND:
        sceKernelLockMutex(cond_mutex, 1, NULL);
        result = sceKernelWaitCond(object, timeout);
        sceKernelUnlockMutex(cond_mutex, 1);
        return result;
    case LW_COND:
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        result = sceKernelWaitLwCond(&lw_cond, timeout);
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        return result;
    case DELAY:
        return sceKernelDelayThread(DELAY_US);
    case THREAD_END:
        return sceKernelWaitThreadEnd(target, NULL, timeout);
    }
    return result;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int k = *(const int *)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    w_result = wait_on(k, &timeout);
    w_elapsed = sceKernelGetProcessTimeWide() - start;
    w_left = timeout;
    w_done = 1;

    SceUInt32 signal_timeout = SIGNAL_WAIT_US;
    const SceUInt64 signal_start = sceKernelGetProcessTimeWide();
    w_sig_result = sceKernelWaitSignal(0, 0, &signal_timeout);
    w_sig_at_once = sceKernelGetProcessTimeWide() - signal_start < AT_ONCE_US;
    return 0;
}

static void create_object(int k) {
    switch (k) {
    case SEMA:
        object = sceKernelCreateSema("sema", 0, 0, 1, NULL);
        break;
    case EVENT_FLAG:
        object = sceKernelCreateEventFlag("evf", 0, 0, NULL);
        break;
    case MUTEX:
        object = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(object, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case WRITE_LOCK:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case SIMPLE_EVENT:
        object = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
        break;
    case TIMER:
        object = sceKernelCreateTimer("timer", 0, NULL);
        break;
    case RECEIVE:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
        break;
    case COND:
        cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
        object = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
        break;
    case LW_COND:
        sceKernelCreateLwMutex(&lw_mutex, "lw cond mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_mutex, NULL);
        break;
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = test_start_thread(target_entry);
        break;
    case DELAY:
        break;
    }
}

// Lets the waiter go on, the way each kind is released
static void release_object(int k) {
    switch (k) {
    case SEMA:
        sceKernelSignalSema(object, 1);
        break;
    case EVENT_FLAG:
        sceKernelSetEventFlag(object, 0x1);
        break;
    case MUTEX:
        sceKernelUnlockMutex(object, 1);
        break;
    case LW_MUTEX:
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case WRITE_LOCK:
        sceKernelUnlockWriteRWLock(object);
        break;
    case SIMPLE_EVENT:
        sceKernelSetEvent(object, EVENT_BIT_A, 0);
        break;
    case TIMER: {
        SceUInt64 interval = TIMER_INTERVAL_US;
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        sceKernelStartTimer(object);
        break;
    }
    case RECEIVE: {
        char message[4] = { 'a', 'b', 'c', 'd' };
        sceKernelSendMsgPipe(object, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL, NULL);
        break;
    }
    case COND:
        sceKernelSignalCond(object);
        break;
    case LW_COND:
        sceKernelSignalLwCond(&lw_cond);
        break;
    case THREAD_END:
        sceKernelSignalSema(gate, 1);
        break;
    case DELAY:
        break;
    }
}

// Returns how many threads wait on the object, or -1 for a kind without a count
static int waiters_of(int k) {
    switch (k) {
    case SEMA: {
        SceKernelSemaInfo info = { .size = sizeof(info) };
        sceKernelGetSemaInfo(object, &info);
        return info.numWaitThreads;
    }
    case EVENT_FLAG: {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        sceKernelGetEventFlagInfo(object, &info);
        return info.numWaitThreads;
    }
    case MUTEX: {
        MutexInfo info = { .size = sizeof(info) };
        sceKernelGetMutexInfo(object, (SceKernelMutexInfo *)&info);
        return info.numWaitThreads;
    }
    case LW_MUTEX: {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &info);
        return info.numWaitThreads;
    }
    case WRITE_LOCK: {
        SceKernelRWLockInfo info = { .size = sizeof(info) };
        sceKernelGetRWLockInfo(object, &info);
        return info.numWriteWaitThreads;
    }
    case SIMPLE_EVENT:
    case TIMER: {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        return info.numWaitThreads;
    }
    case RECEIVE: {
        SceKernelMppInfo info = { .size = sizeof(info) };
        sceKernelGetMsgPipeInfo(object, &info);
        return info.numReceiveWaitThreads;
    }
    case COND: {
        SceKernelCondInfo info = { .size = sizeof(info) };
        sceKernelGetCondInfo(object, &info);
        return info.numWaitThreads;
    }
    case LW_COND: {
        LwCondInfo info = { .size = sizeof(info) };
        sceKernelGetLwCondInfo(&lw_cond, &info);
        return info.numWaitThreads;
    }
    case DELAY:
    case THREAD_END:
        return -1;
    }
    return -1;
}

static void delete_object(int k) {
    switch (k) {
    case SEMA:
        sceKernelDeleteSema(object);
        break;
    case EVENT_FLAG:
        sceKernelDeleteEventFlag(object);
        break;
    case MUTEX:
        sceKernelDeleteMutex(object);
        break;
    case LW_MUTEX:
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case WRITE_LOCK:
        sceKernelDeleteRWLock(object);
        break;
    case SIMPLE_EVENT:
        sceKernelDeleteSimpleEvent(object);
        break;
    case TIMER:
        sceKernelDeleteTimer(object);
        break;
    case RECEIVE:
        sceKernelDeleteMsgPipe(object);
        break;
    case COND:
        sceKernelDeleteCond(object);
        sceKernelDeleteMutex(cond_mutex);
        break;
    case LW_COND:
        sceKernelDeleteLwCond(&lw_cond);
        sceKernelDeleteLwMutex(&lw_mutex);
        break;
    case THREAD_END:
        sceKernelDeleteSema(gate);
        break;
    case DELAY:
        break;
    }
}

static void run_case(int k) {
    w_done = 0;
    create_object(k);
    const SceUID waiter_thread = test_start_thread_arg(waiter, k);
    TEST_WAIT_FOR(thread_is_waiting(waiter_thread));
    sceKernelSendSignal(waiter_thread);
    sceKernelDelayThread(100000);

    // W is blocked until the release, so these reads see its state
    const int still_waiting = !w_done;
    const int waiters = waiters_of(k);
    release_object(k);
    test_join_thread(waiter_thread);
    if (k == THREAD_END)
        test_join_thread(target);
    delete_object(k);

    const char *name = kind_names[k];
    PRINTF(still_waiting, "%s: still waiting after a signal", name);
    if (waiters >= 0)
        PRINTF(waiters, "%s: threads waiting on it", name);
    PRINTF(w_result, "%s: the wait's result", name);
    if (k == DELAY)
        PRINTF(w_elapsed >= 500000, "%s: the delay lasted at least 500 ms", name);
    else
        PRINTF(w_left > 1000000, "%s: its time left is above 1 s", name);
    PRINTF(w_sig_result, "%s: the wait for the signal after it", name);
    PRINTF(w_sig_at_once, "%s: that wait returned at once", name);
}

int main(void) {
    for (int k = 0; k < KIND_COUNT; k++)
        run_case(k);

    test_exit();
    return 0;
}
