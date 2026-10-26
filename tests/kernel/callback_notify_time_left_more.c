// When the time left of a wait with callbacks and a timeout is fixed, if a callback is notified to
// the waiting thread: at the notify, or later when the thread gets the CPU to run the callback.
// This is callback_notify_time_left for the kinds it leaves out. The model covers a semaphore and
// an event flag, and each kind here gets the same two cases as its semaphore: the release after the
// notify, with the main thread keeping the core busy for 500 ms before it, or after it.
//
// Thread T waits with callbacks for 3 s. The notify ends T's wait for the callback, but T can't run
// it until the main thread blocks, so the two moments are 500 ms apart. Before T runs, the main
// thread releases what T waits for. After its callback, T takes it at once in most kinds and its
// wait returns 0 without blocking again, so the time left it writes back to its timeout is the one
// fixed earlier. For the timer, condition, lightweight condition and thread end kinds the retry
// can't take at once in the notify first cases, since the release comes only after the busy time
// and T waits again, so there its time left is the one when it wakes. The simple event wait
// returns the notify error instead of taking again.
// Every thread is pinned to core 0, and T runs at 0xB0, below the main thread at 0x60. The main
// thread keeps the core busy for 500 ms by calling sceKernelGetProcessTimeWide in a loop, which
// doesn't block, so T stays ready the whole time. The cases, each with a fresh object and a fresh
// T:
// - mutex, lightweight mutex, read lock and write lock: the main thread holds the lock, T waits for
//   it with callbacks, and the release is the unlock of the main thread's hold. T unlocks what it
//   got once its wait returned
// - simple event: T waits for EVENT_BIT_A, and the release sets it
// - timer: T waits for EVENT_TIMER, and the timer is set to fire after 100 ms. The release starts
//   it
// - message pipe receive: T receives 4 bytes from an empty pipe, and the release sends 4 bytes
// - message pipe send: T sends 4 bytes into a full pipe, and the release receives 4 bytes
// - condition and lightweight condition: T waits for a signal with its mutex locked. A signal with
//   no waiter is lost, so the release signals once T waits again. The notify first case then busies
//   the main thread for 500 ms after T waits again, and the busy first case signals right after
// - thread signal: T waits in sceKernelWaitSignalCB, and the release is sceKernelSendSignal
// - thread end: T waits for a target thread, which waits for a gate semaphore, and the release
//   signals the gate so that the target returns
//
// T records the time right before its wait. The main thread records the time of the notify and
// computes what T's time left would be at that time, the 3 s minus the time from T's start to the
// notify. Each case prints whether T waits when the main thread notifies, T's wait result, how
// often T's callback ran, whether T's time left is within 200 ms of the one at the notify, and
// whether it is at least 300 ms less than the one at the notify. If the time left is fixed at the
// notify, the first of the two comparisons holds and the second doesn't. If it is fixed when T runs
// or wakes, it is the other way round in the notify first cases, since there the release comes 500
// ms after the notify.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define T_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define WAIT_US 3000000
#define BUSY_US 500000
#define NEAR_US 200000
#define LESS_US 300000
#define TIMER_INTERVAL_US 100000
#define PIPE_SIZE 4096
#define MSG_SIZE 4

typedef enum Kind {
    MUTEX,
    LW_MUTEX,
    READ_LOCK,
    WRITE_LOCK,
    SIMPLE_EVENT,
    TIMER,
    PIPE_RECEIVE,
    PIPE_SEND,
    COND,
    LW_COND,
    SIGNAL,
    THREAD_END
} Kind;

static Kind kind;
static SceUID object, cond_mutex, gate, target, t_thread, callback;
static SceKernelLwMutexWork lw_mutex, lw_cond_mutex;
static SceKernelLwCondWork lw_cond;
static char t_buf[MSG_SIZE], main_buf[MSG_SIZE] = { 'a', 'b', 'c', 'd' }, fill[PIPE_SIZE];
static volatile int wait_result, callback_runs;
static volatile SceUInt time_left;
static volatile SceUInt64 t_start;

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    return 0;
}

// Waits for the gate, then returns, so a thread end wait ends
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Returns the UID that a wait on the lightweight mutex reports
static SceUID lw_mutex_uid(SceKernelLwMutexWork *work) {
    LwMutexInfo info = { .size = sizeof(info) };
    return sceKernelGetLwMutexInfo(work, &info) == 0 ? info.uid : -1;
}

// Returns the UID that a wait on the lightweight condition reports
static SceUID lw_cond_uid(SceKernelLwCondWork *work) {
    LwCondInfo info = { .size = sizeof(info) };
    return sceKernelGetLwCondInfo(work, &info) == 0 ? info.uid : -1;
}

// Returns the wait type of the kind, without the callback bit
static SceUInt32 wait_type(void) {
    switch (kind) {
    case MUTEX: return WAIT_TYPE_MUTEX;
    case LW_MUTEX: return WAIT_TYPE_LW_MUTEX;
    case READ_LOCK:
    case WRITE_LOCK: return WAIT_TYPE_RW_LOCK;
    case SIMPLE_EVENT:
    case TIMER: return WAIT_TYPE_EVENT;
    case PIPE_RECEIVE:
    case PIPE_SEND: return WAIT_TYPE_MSG_PIPE;
    case COND: return WAIT_TYPE_COND;
    case LW_COND: return WAIT_TYPE_LW_COND;
    case SIGNAL: return WAIT_TYPE_SIGNAL;
    case THREAD_END: return WAIT_TYPE_THREAD_END;
    }
    return 0;
}

// Returns whether thread t waits with callbacks in the wait of the kind, on the object for the
// kinds that have one
static int t_waits(SceUID t) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(t, &info) != 0 || info.waitType != (wait_type() | WAIT_TYPE_CB))
        return 0;
    return kind == SIGNAL || kind == THREAD_END || info.waitId == object;
}

// Returns whether the kind is a condition, whose signal is lost unless a thread waits for it
static int is_cond(void) {
    return kind == COND || kind == LW_COND;
}

// Creates the object of the kind, locked by the main thread where T waits for a lock
static void create_object(void) {
    SceUInt timeout = WAIT_US;
    switch (kind) {
    case MUTEX:
        object = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(object, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        object = lw_mutex_uid(&lw_mutex);
        break;
    case READ_LOCK:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case WRITE_LOCK:
        object = sceKernelCreateRWLock("rwlock", 0, NULL);
        sceKernelLockReadRWLock(object, NULL);
        break;
    case SIMPLE_EVENT: object = sceKernelCreateSimpleEvent("event", 0, 0, NULL); break;
    case TIMER: {
        object = sceKernelCreateTimer("timer", 0, NULL);
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
    case SIGNAL: break;
    case THREAD_END:
        gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
        target = sceKernelCreateThread("target", target_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(target, 0, NULL);
        break;
    }
}

static void delete_object(void) {
    switch (kind) {
    case MUTEX: sceKernelDeleteMutex(object); break;
    case LW_MUTEX: sceKernelDeleteLwMutex(&lw_mutex); break;
    case READ_LOCK:
    case WRITE_LOCK: sceKernelDeleteRWLock(object); break;
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
    case SIGNAL: break;
    case THREAD_END: sceKernelDeleteSema(gate); break;
    }
}

// Unlocks what T got with a successful lock wait
static void give_back(void) {
    switch (kind) {
    case MUTEX: sceKernelUnlockMutex(object, 1); break;
    case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
    case READ_LOCK: sceKernelUnlockReadRWLock(object); break;
    case WRITE_LOCK: sceKernelUnlockWriteRWLock(object); break;
    default: break;
    }
}

// Gives T what it waits for
static void release(void) {
    SceUInt timeout = WAIT_US;
    switch (kind) {
    case MUTEX: sceKernelUnlockMutex(object, 1); break;
    case LW_MUTEX: sceKernelUnlockLwMutex(&lw_mutex, 1); break;
    case READ_LOCK: sceKernelUnlockWriteRWLock(object); break;
    case WRITE_LOCK: sceKernelUnlockReadRWLock(object); break;
    case SIMPLE_EVENT: sceKernelSetEvent(object, EVENT_BIT_A, 0); break;
    case TIMER:
        sceKernelStartTimer(object);
        break;
    case PIPE_RECEIVE: sceKernelSendMsgPipe(object, main_buf, sizeof(main_buf), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case PIPE_SEND: sceKernelReceiveMsgPipe(object, main_buf, sizeof(main_buf), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case COND: sceKernelSignalCond(object); break;
    case LW_COND: sceKernelSignalLwCond(&lw_cond); break;
    case SIGNAL: sceKernelSendSignal(t_thread); break;
    case THREAD_END: sceKernelSignalSema(gate, 1); break;
    }
}

// Waits with callbacks for what the kind waits for, records the time left and the result, and does
// what the kind needs after a successful wait
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    SceUInt timeout = WAIT_US;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    if (kind == COND)
        sceKernelLockMutex(cond_mutex, 1, NULL);
    if (kind == LW_COND)
        sceKernelLockLwMutex(&lw_cond_mutex, 1, NULL);
    t_start = sceKernelGetProcessTimeWide();
    switch (kind) {
    case MUTEX: wait_result = sceKernelLockMutexCB(object, 1, &timeout); break;
    case LW_MUTEX: wait_result = sceKernelLockLwMutexCB(&lw_mutex, 1, &timeout); break;
    case READ_LOCK: wait_result = sceKernelLockReadRWLockCB(object, &timeout); break;
    case WRITE_LOCK: wait_result = sceKernelLockWriteRWLockCB(object, &timeout); break;
    case SIMPLE_EVENT:
        wait_result = sceKernelWaitEventCB(object, EVENT_BIT_A, &pattern, &user_data, &timeout);
        break;
    case TIMER:
        wait_result = sceKernelWaitEventCB(object, EVENT_TIMER, &pattern, &user_data, &timeout);
        break;
    case PIPE_RECEIVE: wait_result = sceKernelReceiveMsgPipeCB(object, t_buf, sizeof(t_buf), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case PIPE_SEND: wait_result = sceKernelSendMsgPipeCB(object, t_buf, sizeof(t_buf), MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case COND: wait_result = sceKernelWaitCondCB(object, &timeout); break;
    case LW_COND: wait_result = sceKernelWaitLwCondCB(&lw_cond, &timeout); break;
    case SIGNAL: wait_result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case THREAD_END: wait_result = sceKernelWaitThreadEndCB(target, NULL, &timeout); break;
    }
    time_left = timeout;
    if (wait_result == 0)
        give_back();
    if (kind == COND)
        sceKernelUnlockMutex(cond_mutex, 1);
    if (kind == LW_COND)
        sceKernelUnlockLwMutex(&lw_cond_mutex, 1);
    sceKernelDeleteCallback(callback);
    return 0;
}

// Starts a thread on core 0 below the main thread
static SceUID start(const char *name, SceKernelThreadEntry entry) {
    const SceUID thread = sceKernelCreateThread(name, entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Keeps the core busy without blocking for the given time
static void keep_busy(SceUInt64 us) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + us;
    while (sceKernelGetProcessTimeWide() < end)
        ;
}

static void run_case(const char *name, Kind k, int busy_first) {
    kind = k;
    callback = 0;
    callback_runs = 0;
    wait_result = NOT_DONE;
    time_left = NOT_DONE;
    t_start = 0;
    create_object();
    t_thread = start("t", t_entry);
    TEST_WAIT_FOR(t_waits(t_thread));
    const int waits = t_waits(t_thread);
    if (busy_first)
        keep_busy(BUSY_US);
    const SceUInt64 notify_time = sceKernelGetProcessTimeWide();
    sceKernelNotifyCallback(callback, 1);
    if (is_cond())
        TEST_WAIT_FOR(callback_runs > 0 && t_waits(t_thread));
    if (!busy_first)
        keep_busy(BUSY_US);
    release();
    test_join_thread(t_thread);
    if (k == THREAD_END)
        test_join_thread(target);
    delete_object();

    const SceInt64 at_notify = (SceInt64)WAIT_US - (SceInt64)(notify_time - t_start);
    const SceInt64 less = at_notify - (SceInt64)time_left;
    PRINTF(waits, "%s: T waits when the main thread notifies", name);
    PRINTF(wait_result, "%s: T's wait result", name);
    PRINTF(callback_runs, "%s: runs of T's callback", name);
    PRINTF(less >= -NEAR_US && less <= NEAR_US, "%s: the time left is within 200 ms of the one at the notify", name);
    PRINTF(less >= LESS_US, "%s: the time left is at least 300 ms less than the one at the notify", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("mutex, notify first", MUTEX, 0);
    run_case("mutex, busy first", MUTEX, 1);
    run_case("lightweight mutex, notify first", LW_MUTEX, 0);
    run_case("lightweight mutex, busy first", LW_MUTEX, 1);
    run_case("read lock, notify first", READ_LOCK, 0);
    run_case("read lock, busy first", READ_LOCK, 1);
    run_case("write lock, notify first", WRITE_LOCK, 0);
    run_case("write lock, busy first", WRITE_LOCK, 1);
    run_case("simple event, notify first", SIMPLE_EVENT, 0);
    run_case("simple event, busy first", SIMPLE_EVENT, 1);
    run_case("timer, notify first", TIMER, 0);
    run_case("timer, busy first", TIMER, 1);
    run_case("message pipe receive, notify first", PIPE_RECEIVE, 0);
    run_case("message pipe receive, busy first", PIPE_RECEIVE, 1);
    run_case("message pipe send, notify first", PIPE_SEND, 0);
    run_case("message pipe send, busy first", PIPE_SEND, 1);
    run_case("condition, notify first", COND, 0);
    run_case("condition, busy first", COND, 1);
    run_case("lightweight condition, notify first", LW_COND, 0);
    run_case("lightweight condition, busy first", LW_COND, 1);
    run_case("thread signal, notify first", SIGNAL, 0);
    run_case("thread signal, busy first", SIGNAL, 1);
    run_case("thread end, notify first", THREAD_END, 0);
    run_case("thread end, busy first", THREAD_END, 1);

    test_exit();
    return 0;
}
