// The wait kinds callback_after_wake leaves out, with the same cases. Whether a callback that is
// notified after a wait with callbacks was ended by a wake, but before the waiting thread ran
// again, runs before that wait returns or stays pending. The waiter T waits on the kind with
// callbacks, the main thread ends the wait, notifies T's callback, and then T runs. T runs on
// core 0 at 0xB0 under a gate thread that spins on core 0 at the highest user priority from the
// moment T waits until the test lets go, so T stays ready between the wake and the notify. The
// main thread runs on core 1. The kinds, and how the main thread ends the wait:
// - event flag: sets the flag's bit
// - mutex and lightweight mutex: unlocks the lock it holds, which hands it to T
// - read lock and write lock: unlocks the write lock it holds, which hands the lock to T
// - simple event: sets its bit
// - timer: starts it, and waits until T leaves its wait
// - message pipe receive: sends 4 bytes into the empty pipe
// - message pipe send: receives 4 bytes from the full pipe, which makes room for T's send
// - delay with callbacks: waits until T's delay ends
// Each case prints whether T waits when the main thread wakes it, the wake and the notify, T's
// status after each, the wait's result, how often the callback ran when the wait returned, the
// check's result and how often the callback ran after it. For a lock, T's unlock is printed too.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define SPINS 400000000
#define T_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define TIMER_US 100000
#define DELAY_US 200000
#define PIPE_SIZE 0x1000
#define MSG_SIZE 4

typedef enum Kind {
    EVENT_FLAG,
    MUTEX,
    LW_MUTEX,
    READ_LOCK,
    WRITE_LOCK,
    SIMPLE_EVENT,
    TIMER,
    MSG_RECEIVE,
    MSG_SEND,
    DELAY
} Kind;

static Kind kind;
static SceUID flag, mutex, rwlock, event, timer, pipe, gate, callback;
static SceKernelLwMutexWork lw_mutex;
static volatile int gate_up, gate_open, callback_ready, callback_runs, done;
static volatile int wait_result, runs_in_wait, check_result, runs_after_check, unlock_result;
static char message[MSG_SIZE] = { 1, 2, 3, 4 };
static char got[MSG_SIZE];
static char fill[PIPE_SIZE];

static int is_lock(Kind k);
static int unlock_held(Kind k);

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

static void gate_lift(void) {
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
}

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Waits on the kind with callbacks, and checks the callback and unlocks the lock it got
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    SceUInt32 bits = 0;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    callback_ready = 1;
    switch (kind) {
    case EVENT_FLAG: wait_result = sceKernelWaitEventFlagCB(flag, 0x1, SCE_EVENT_WAITAND, &bits, &timeout); break;
    case MUTEX: wait_result = sceKernelLockMutexCB(mutex, 1, &timeout); break;
    case LW_MUTEX: wait_result = sceKernelLockLwMutexCB(&lw_mutex, 1, &timeout); break;
    case READ_LOCK: wait_result = sceKernelLockReadRWLockCB(rwlock, &timeout); break;
    case WRITE_LOCK: wait_result = sceKernelLockWriteRWLockCB(rwlock, &timeout); break;
    case SIMPLE_EVENT: wait_result = sceKernelWaitEventCB(event, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    case TIMER: wait_result = sceKernelWaitEventCB(timer, EVENT_TIMER, &pattern, &user_data, &timeout); break;
    case MSG_RECEIVE:
        wait_result = sceKernelReceiveMsgPipeCB(pipe, got, sizeof(got), MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case MSG_SEND:
        wait_result = sceKernelSendMsgPipeCB(pipe, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case DELAY: wait_result = sceKernelDelayThreadCB(DELAY_US); break;
    }
    runs_in_wait = callback_runs;
    check_result = sceKernelCheckCallback();
    runs_after_check = callback_runs;
    if (is_lock(kind) && wait_result == 0)
        unlock_result = unlock_held(kind);
    sceKernelDeleteCallback(callback);
    done = 1;
    return 0;
}

static SceUID start_t(void) {
    const SceUID thread = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

// The wait type of the kind's wait with callbacks
static SceUInt32 wait_type_of(Kind k) {
    switch (k) {
    case EVENT_FLAG: return WAIT_TYPE_EVENT_FLAG | WAIT_TYPE_CB;
    case MUTEX: return WAIT_TYPE_MUTEX | WAIT_TYPE_CB;
    case LW_MUTEX: return WAIT_TYPE_LW_MUTEX | WAIT_TYPE_CB;
    case READ_LOCK:
    case WRITE_LOCK: return WAIT_TYPE_RW_LOCK | WAIT_TYPE_CB;
    case SIMPLE_EVENT:
    case TIMER: return WAIT_TYPE_EVENT | WAIT_TYPE_CB;
    case MSG_RECEIVE:
    case MSG_SEND: return WAIT_TYPE_MSG_PIPE | WAIT_TYPE_CB;
    case DELAY: return WAIT_TYPE_DELAY | WAIT_TYPE_CB;
    }
    return 0;
}

// Returns whether T is in the kind's wait with callbacks. Only the wait type is checked, since each
// case has one waiter.
static int t_waits(SceUID t) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(t, &info) == 0 && info.waitType == wait_type_of(kind);
}

static int in_any_wait(SceUID t) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(t, &info) == 0 && info.waitType != 0;
}

// Waits until T has left its wait, and returns whether it did
static int t_left_wait(SceUID t) {
    TEST_WAIT_FOR(!in_any_wait(t));
    return !in_any_wait(t);
}

static int is_lock(Kind k) {
    return k == MUTEX || k == LW_MUTEX || k == READ_LOCK || k == WRITE_LOCK;
}

// Releases the lock T got, and returns the unlock's result
static int unlock_held(Kind k) {
    switch (k) {
    case MUTEX: return sceKernelUnlockMutex(mutex, 1);
    case LW_MUTEX: return sceKernelUnlockLwMutex(&lw_mutex, 1);
    case READ_LOCK: return sceKernelUnlockReadRWLock(rwlock);
    case WRITE_LOCK: return sceKernelUnlockWriteRWLock(rwlock);
    default: return 0;
    }
}

static void create_objects(Kind k) {
    switch (k) {
    case EVENT_FLAG: flag = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case MUTEX:
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(mutex, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case READ_LOCK:
    case WRITE_LOCK:
        rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockWriteRWLock(rwlock, NULL);
        break;
    case SIMPLE_EVENT: event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL); break;
    case TIMER: {
        timer = sceKernelCreateTimer("timer", EVENT_ATTR_AUTO_RESET, NULL);
        SceUInt64 interval = TIMER_US;
        sceKernelSetTimerEvent(timer, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    }
    case MSG_RECEIVE: pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 64, NULL); break;
    case MSG_SEND:
        pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
        sceKernelTrySendMsgPipe(pipe, fill, sizeof(fill), MSG_PIPE_MODE_FULL, NULL);
        break;
    case DELAY: break;
    }
}

static void delete_objects(Kind k) {
    switch (k) {
    case EVENT_FLAG: sceKernelDeleteEventFlag(flag); break;
    case MUTEX: sceKernelDeleteMutex(mutex); break;
    case LW_MUTEX: sceKernelDeleteLwMutex(&lw_mutex); break;
    case READ_LOCK:
    case WRITE_LOCK: sceKernelDeleteRWLock(rwlock); break;
    case SIMPLE_EVENT: sceKernelDeleteSimpleEvent(event); break;
    case TIMER:
        sceKernelStopTimer(timer);
        sceKernelDeleteTimer(timer);
        break;
    case MSG_RECEIVE:
    case MSG_SEND: sceKernelDeleteMsgPipe(pipe); break;
    case DELAY: break;
    }
}

// Ends the kind's wait from the main thread, and returns what that call returned. A timer or a
// delay ends its wait by its time, so the main thread waits for T to leave the wait.
static int wake_kind(Kind k, SceUID t) {
    switch (k) {
    case EVENT_FLAG: return sceKernelSetEventFlag(flag, 0x1);
    case MUTEX: return sceKernelUnlockMutex(mutex, 1);
    case LW_MUTEX: return sceKernelUnlockLwMutex(&lw_mutex, 1);
    case READ_LOCK:
    case WRITE_LOCK: return sceKernelUnlockWriteRWLock(rwlock);
    case SIMPLE_EVENT: return sceKernelSetEvent(event, EVENT_BIT_A, 0);
    case TIMER: {
        const int start = sceKernelStartTimer(timer);
        t_left_wait(t);
        return start;
    }
    case MSG_RECEIVE: return sceKernelTrySendMsgPipe(pipe, message, sizeof(message), MSG_PIPE_MODE_FULL, NULL);
    // A blocking receive, since a try receive that leaves a sender waiting can hang the console
    case MSG_SEND: return sceKernelReceiveMsgPipe(pipe, got, sizeof(got), MSG_PIPE_MODE_FULL, NULL, NULL);
    case DELAY: return t_left_wait(t);
    }
    return 0;
}

static void run_case(const char *name, Kind k) {
    kind = k;
    wait_result = runs_in_wait = check_result = runs_after_check = unlock_result = NOT_DONE;
    callback_ready = callback_runs = done = 0;
    create_objects(k);
    const SceUID t = start_t();
    TEST_WAIT_FOR(callback_ready && t_waits(t));
    gate_close();
    const int waiting = t_waits(t);
    const int wake = wake_kind(k, t);
    const int status_wake = status_of(t);
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int status_notify = status_of(t);
    gate_lift();
    TEST_WAIT_FOR(done);
    test_join_thread(t);
    delete_objects(k);

    PRINTF(waiting, "%s: T waits when the main thread wakes it", name);
    PRINTF(wake, "%s: the wake", name);
    PRINTF(status_wake, "%s: T's status after the wake", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(status_notify, "%s: T's status after the notify", name);
    PRINTF(wait_result, "%s: the wait's result", name);
    PRINTF(runs_in_wait, "%s: runs of the callback when the wait returned", name);
    PRINTF(check_result, "%s: the check's result", name);
    PRINTF(runs_after_check, "%s: runs of the callback after the check", name);
    if (is_lock(k))
        PRINTF(unlock_result, "%s: T's unlock", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("event flag", EVENT_FLAG);
    run_case("mutex", MUTEX);
    run_case("lightweight mutex", LW_MUTEX);
    run_case("read lock", READ_LOCK);
    run_case("write lock", WRITE_LOCK);
    run_case("simple event", SIMPLE_EVENT);
    run_case("timer", TIMER);
    run_case("message pipe receive", MSG_RECEIVE);
    run_case("message pipe send", MSG_SEND);
    run_case("delay", DELAY);

    test_exit();
    return 0;
}
