// What a callback notified while a condition variable waiter waits to lock its mutex again does to
// that relock. Thread T locks a mutex M and waits on a condition C with callbacks and a timeout of
// 8 s, which releases M. The main thread locks M and signals C, so T waits to lock M again, and
// once T waits for M the main thread notifies T's callback. The cases differ in what happens next.
// Each case runs for a heavy mutex and condition and for a lightweight pair, on fresh objects and
// threads, except case f, which only runs for the lightweight pair. The threads are pinned to core
// 0. T and the locker L run below the main thread, except in case d, where T runs above it. The
// cases:
// - a: right after the notify the main thread reads whether T waits for M, the condition's waiters
//   and the result of SignalCondTo(T). Once the callback blocks on a gate, it reads T's state and
//   the waiters again, opens the gate, and unlocks M once T waits for M again
// - b: once the callback blocks on the gate, the main thread unlocks M, tries to lock it, unlocks
//   it again if the try lock worked, and opens the gate
// - c: a plain locker L waits for M behind T. After the notify the main thread waits until the
//   callback blocks on the gate, then unlocks M, which goes to L and not to T. Once L holds M and
//   waits on its own gate, the main thread opens the callback's gate, so T waits for M behind L.
//   Then L's gate opens, L unlocks M, and T takes it
// - d: T runs above the main thread. The main thread records steps around the notify, then waits
//   until the callback ran and T waits for M again, reads T's state, and unlocks M
// - f: the main thread unlocks M, which hands it to T, then notifies T's callback. T waits on a
//   gate right after its wait returns, so the notify comes while T is held there. Lightweight only.
// Each case prints the results of the calls and the states the main thread reads, the steps of case
// d, the order in which the threads took M as hex digits, and T's result, the runs of the callback
// when its wait returned, whether T owns M after the wait, and the results of T's unlock and of
// sceKernelCheckCallback. T's time left in case b is printed as a comparison.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define ABOVE_PRIORITY 0x50
#define WAIT_US 8000000
#define GATE_US 3000000
#define NOT_DONE 0xDEAD
#define STEP_COUNT 3
#define LW_SLOTS 5

// Whether the running case uses the lightweight pair. The heavy pair is used otherwise.
static int lightweight;
static SceUID mutex, cond, mutex_id, callback, gate, l_gate, hold;
// Each lightweight case gets its own mutex work area, since a lightweight mutex is never deleted
static SceKernelLwMutexWork lw_mutexes[LW_SLOTS];
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork lw_cond;
static int lw_slot;

static volatile int gated, held, callback_ready, callback_runs, t_done;
static volatile int t_result, t_left, t_runs, t_owns, t_unlock, t_check, t_check_runs;
static volatile int l_lock, l_unlock, order;
static int step_count, steps[STEP_COUNT];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < STEP_COUNT)
        steps[i] = step;
}

// Counts its run, and in a gated case blocks on the gate until the main thread opens it
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    record(1);
    if (gated) {
        SceUInt timeout = GATE_US;
        sceKernelWaitSema(gate, 1, &timeout);
    }
    return 0;
}

static int lock_mutex(void) {
    return lightweight ? sceKernelLockLwMutex(lw_mutex, 1, NULL) : sceKernelLockMutex(mutex, 1, NULL);
}

static int try_lock_mutex(void) {
    return lightweight ? sceKernelTryLockLwMutex(lw_mutex, 1) : sceKernelTryLockMutex(mutex, 1);
}

static int unlock_mutex(void) {
    return lightweight ? sceKernelUnlockLwMutex(lw_mutex, 1) : sceKernelUnlockMutex(mutex, 1);
}

static int signal_cond(void) {
    return lightweight ? sceKernelSignalLwCond(&lw_cond) : sceKernelSignalCond(cond);
}

static int signal_cond_to(SceUID thread) {
    return lightweight ? sceKernelSignalLwCondTo(&lw_cond, thread) : sceKernelSignalCondTo(cond, thread);
}

static int wait_cond_cb(SceUInt *timeout) {
    return lightweight ? sceKernelWaitLwCondCB(&lw_cond, timeout) : sceKernelWaitCondCB(cond, timeout);
}

// Returns the owner of the mutex
static SceUID mutex_owner(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        return info.currentOwnerId;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId;
}

// Returns the number of threads waiting for the mutex, which a relocking waiter counts
static int mutex_waiters(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        return info.numWaitThreads;
    }
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.numWaitThreads;
}

// Returns the number of threads waiting on the condition
static int cond_waiters(void) {
    if (lightweight) {
        LwCondInfo info = { .size = sizeof(info) };
        sceKernelGetLwCondInfo(&lw_cond, &info);
        return info.numWaitThreads;
    }
    SceKernelCondInfo info = { .size = sizeof(info) };
    sceKernelGetCondInfo(cond, &info);
    return info.numWaitThreads;
}

// Returns whether thread is in a wait of the given type, on id unless id is 0
static int waits_in(SceUID thread, SceUInt32 type, SceUID id) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type && (id == 0 || info.waitId == id);
}

static SceUInt32 cond_wait_type(void) {
    return (lightweight ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND) | WAIT_TYPE_CB;
}

static SceUInt32 relock_wait_type(void) {
    return (lightweight ? WAIT_TYPE_LW_COND_MUTEX : WAIT_TYPE_COND_MUTEX) | WAIT_TYPE_CB;
}

static SceUInt32 lock_wait_type(void) {
    return lightweight ? WAIT_TYPE_LW_MUTEX : WAIT_TYPE_MUTEX;
}

// Returns whether thread waits with callbacks for the condition. A lightweight condition's UID
// isn't compared, so only its wait type counts.
static int in_cond(SceUID thread) {
    return waits_in(thread, cond_wait_type(), lightweight ? 0 : cond);
}

// Returns whether thread waits with callbacks to lock the mutex again after its signal
static int in_relock(SceUID thread) {
    return waits_in(thread, relock_wait_type(), mutex_id);
}

// Returns the thread info, which holds the status and the wait type the cases print
static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

static const char *kind_name(void) {
    return lightweight ? "lightweight" : "heavy";
}

static SceUID start_thread(SceKernelThreadEntry entry, int priority) {
    const SceUID thread = sceKernelCreateThread("t", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    return thread;
}

// Waits up to 5 s for a thread to end, and deletes it if it did
static void join_thread(SceUID thread) {
    SceUInt timeout = 5 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) == 0)
        sceKernelDeleteThread(thread);
}

// Creates the mutex and the condition of the case, or a lightweight pair with a fresh work area
static void create_objects(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    l_gate = sceKernelCreateSema("l gate", 0, 0, 1, NULL);
    if (lightweight) {
        lw_mutex = &lw_mutexes[lw_slot++];
        sceKernelCreateLwMutex(lw_mutex, "lw mutex", 0, 0, NULL);
        sceKernelCreateLwCond(&lw_cond, "lw cond", 0, lw_mutex, NULL);
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(lw_mutex, &info);
        mutex_id = info.uid;
    } else {
        mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
        cond = sceKernelCreateCond("cond", 0, mutex, NULL);
        mutex_id = mutex;
    }
}

// Deletes the condition and the gates. A lightweight mutex is never deleted.
static void destroy_objects(void) {
    sceKernelDeleteSema(gate);
    sceKernelDeleteSema(l_gate);
    if (lightweight) {
        sceKernelDeleteLwCond(&lw_cond);
    } else {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    }
}

static void reset_case(int with_gate) {
    gated = with_gate;
    held = 0;
    callback = 0;
    callback_ready = callback_runs = t_done = 0;
    t_result = t_left = t_runs = t_owns = t_unlock = t_check = t_check_runs = NOT_DONE;
    l_lock = l_unlock = NOT_DONE;
    order = 0;
    step_count = 0;
    for (int i = 0; i < STEP_COUNT; i++)
        steps[i] = NOT_DONE;
}

// Locks M, waits on C with callbacks, then records how the wait ended and what the thread holds
static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    lock_mutex();
    callback_ready = 1;
    SceUInt timeout = WAIT_US;
    t_result = wait_cond_cb(&timeout);
    t_left = timeout;
    t_runs = callback_runs;
    if (held) {
        SceUInt hold_timeout = GATE_US;
        sceKernelWaitSema(hold, 1, &hold_timeout);
    }
    t_owns = mutex_owner() == sceKernelGetThreadId();
    if (t_owns)
        order = (order << 4) | 2;
    t_unlock = unlock_mutex();
    t_check = sceKernelCheckCallback();
    t_check_runs = callback_runs;
    sceKernelDeleteCallback(callback);
    t_done = 1;
    return 0;
}

// Waits for the mutex behind T, holds it until the main thread opens its gate, then unlocks it
static int l_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    l_lock = lock_mutex();
    if (l_lock == 0)
        order = (order << 4) | 1;
    SceUInt timeout = GATE_US;
    sceKernelWaitSema(l_gate, 1, &timeout);
    l_unlock = unlock_mutex();
    return 0;
}

// Notifies the callback while the main thread holds M, reads what the notify changed, then reads
// T's state while the callback is blocked on the gate. The gate opens, T waits for M again, and the
// main thread unlocks M, which T takes.
static void case_reads(void) {
    const char *const kind = kind_name();
    reset_case(1);
    create_objects();
    const SceUID t = start_thread(t_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int at_once_relock = in_relock(t);
    const int at_once_cond_waiters = cond_waiters();
    const int at_once_signal_to = signal_cond_to(t);
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, gate));
    const int on_gate = waits_in(t, WAIT_TYPE_SEMA, gate);
    const SceKernelThreadInfo in_gate = info_of(t);
    const int gate_relock = in_relock(t);
    const int gate_mutex_waiters = mutex_waiters();
    const int gate_cond_waiters = cond_waiters();
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(in_relock(t));
    const int waits_again = in_relock(t);
    const int unlock = unlock_mutex();
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s, a: the signal", kind);
    PRINTF(notify, "%s, a: the notify", kind);
    PRINTF(at_once_relock, "%s, a: T waits for the mutex right after the notify", kind);
    PRINTF(at_once_cond_waiters, "%s, a: the condition's waiters right after the notify", kind);
    PRINTF(at_once_signal_to, "%s, a: SignalCondTo(T) right after the notify", kind);
    PRINTF(on_gate, "%s, a: T waits on the gate in the callback", kind);
    PRINTF(in_gate.status, "%s, a: T's status in the callback", kind);
    PRINTF(in_gate.waitType, "%s, a: T's wait type in the callback", kind);
    PRINTF(gate_relock, "%s, a: T waits for the mutex in the callback", kind);
    PRINTF(gate_mutex_waiters, "%s, a: the mutex's waiters in the callback", kind);
    PRINTF(gate_cond_waiters, "%s, a: the condition's waiters in the callback", kind);
    PRINTF(waits_again, "%s, a: T waits for the mutex again after the callback", kind);
    PRINTF(unlock, "%s, a: the main thread's unlock", kind);
    PRINTF(t_result, "%s, a: T's wait", kind);
    PRINTF(t_runs, "%s, a: runs of the callback when the wait returned", kind);
    PRINTF(t_owns, "%s, a: T owns the mutex after the wait", kind);
    PRINTF(t_unlock, "%s, a: T's unlock", kind);
    PRINTF(t_check, "%s, a: T's check", kind);
}

// Unlocks M while the callback is blocked on the gate, nobody else waits for M, then tries to lock
// M, unlocks it again if that worked, and opens the gate. T's time left is printed as a comparison.
static void case_unlock(void) {
    const char *const kind = kind_name();
    reset_case(1);
    create_objects();
    const SceUID t = start_thread(t_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, gate));
    const int unlock = unlock_mutex();
    const int try_lock = try_lock_mutex();
    int unlock_after_try = NOT_DONE;
    if (try_lock == 0)
        unlock_after_try = unlock_mutex();
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s, b: the signal", kind);
    PRINTF(notify, "%s, b: the notify", kind);
    PRINTF(unlock, "%s, b: the main thread's unlock while the callback waits", kind);
    PRINTF(try_lock, "%s, b: the main thread's try lock", kind);
    if (try_lock == 0)
        PRINTF(unlock_after_try, "%s, b: the main thread's unlock after the try lock", kind);
    PRINTF(t_result, "%s, b: T's wait", kind);
    PRINTF(t_runs, "%s, b: runs of the callback when the wait returned", kind);
    PRINTF(t_left > 3 * WAIT_US / 4, "%s, b: T's time left is above 75 percent of its timeout", kind);
    PRINTF(t_owns, "%s, b: T owns the mutex after the wait", kind);
    PRINTF(t_unlock, "%s, b: T's unlock", kind);
    PRINTF(t_check, "%s, b: T's check", kind);
}

// A plain locker L waits for M behind T. The callback is notified and blocks on the gate, then
// the main thread unlocks M, which goes to L. The callback's gate opens, so T waits for M behind
// L. Then L's gate opens, so L unlocks M and T takes it.
static void case_locker(void) {
    const char *const kind = kind_name();
    reset_case(1);
    create_objects();
    const SceUID t = start_thread(t_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    const SceUID l = start_thread(l_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(waits_in(l, lock_wait_type(), lightweight ? 0 : mutex));
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, gate));
    const int unlock = unlock_mutex();
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, gate) && waits_in(l, WAIT_TYPE_SEMA, l_gate));
    const int l_owns = mutex_owner() == l;
    const int waiters_before_gate = mutex_waiters();
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(in_relock(t));
    const int waits_again = in_relock(t);
    const int waiters_while_l_owns = mutex_waiters();
    const int l_still_owns = mutex_owner() == l;
    sceKernelSignalSema(l_gate, 1);
    join_thread(l);
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s, c: the signal", kind);
    PRINTF(notify, "%s, c: the notify", kind);
    PRINTF(unlock, "%s, c: the main thread's unlock", kind);
    PRINTF(l_owns, "%s, c: L owns the mutex after the unlock", kind);
    PRINTF(waiters_before_gate, "%s, c: the mutex's waiters after the unlock", kind);
    PRINTF(waits_again, "%s, c: T waits for the mutex again after the callback", kind);
    PRINTF(waiters_while_l_owns, "%s, c: the mutex's waiters while L owns it", kind);
    PRINTF(l_still_owns, "%s, c: L owns the mutex while T waits", kind);
    PRINTF(l_lock, "%s, c: L's lock", kind);
    PRINTF(l_unlock, "%s, c: L's unlock", kind);
    PRINTF(order, "%s, c: the order the threads took the mutex, as hex digits", kind);
    PRINTF(t_result, "%s, c: T's wait", kind);
    PRINTF(t_runs, "%s, c: runs of the callback when the wait returned", kind);
    PRINTF(t_owns, "%s, c: T owns the mutex after the wait", kind);
    PRINTF(t_unlock, "%s, c: T's unlock", kind);
    PRINTF(t_check, "%s, c: T's check", kind);
}

// T runs above the main thread. The main thread records steps around the notify, then waits until
// the callback ran and T waits for M again, reads T's state, and unlocks M, which T takes.
static void case_above(void) {
    const char *const kind = kind_name();
    reset_case(0);
    create_objects();
    const SceUID t = sceKernelCreateThread("t", t_entry, ABOVE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    record(0);
    const int notify = sceKernelNotifyCallback(callback, 1);
    record(2);
    // Another thread can run T's callback after this, so wait until it ran and T waits again
    TEST_WAIT_FOR(callback_runs != 0 && in_relock(t) && mutex_waiters() == 1);
    const SceKernelThreadInfo after = info_of(t);
    const int relock_after = in_relock(t);
    const int waiters_after = mutex_waiters();
    const int unlock = unlock_mutex();
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();

    PRINTF(signal, "%s, d: the signal", kind);
    for (int i = 0; i < STEP_COUNT; i++)
        PRINTF(steps[i], "%s, d: step %d", kind, i);
    PRINTF(notify, "%s, d: the notify", kind);
    PRINTF(after.waitType, "%s, d: T's wait type after the notify, once it waits again", kind);
    PRINTF(relock_after, "%s, d: T waits for the mutex after the notify", kind);
    PRINTF(waiters_after, "%s, d: the mutex's waiters after the notify", kind);
    PRINTF(callback_runs, "%s, d: runs of the callback after the notify", kind);
    PRINTF(unlock, "%s, d: the main thread's unlock", kind);
    PRINTF(t_result, "%s, d: T's wait", kind);
    PRINTF(t_runs, "%s, d: runs of the callback when the wait returned", kind);
    PRINTF(t_owns, "%s, d: T owns the mutex after the wait", kind);
    PRINTF(t_unlock, "%s, d: T's unlock", kind);
    PRINTF(t_check, "%s, d: T's check", kind);
}

// The main thread unlocks M, which hands it to T, then notifies T's callback. T waits on a gate
// right after its wait returns, and the main thread opens that gate after the notify.
static void case_handover(void) {
    const char *const kind = kind_name();
    reset_case(0);
    held = 1;
    create_objects();
    hold = sceKernelCreateSema("hold", 0, 0, 1, NULL);
    const SceUID t = start_thread(t_entry, WAITER_PRIORITY);
    TEST_WAIT_FOR(callback_ready && in_cond(t));
    lock_mutex();
    const int signal = signal_cond();
    TEST_WAIT_FOR(in_relock(t));
    const int waits_for_mutex = in_relock(t);
    const int unlock = unlock_mutex();
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(waits_in(t, WAIT_TYPE_SEMA, hold));
    sceKernelSignalSema(hold, 1);
    TEST_WAIT_FOR(t_done);
    join_thread(t);
    destroy_objects();
    sceKernelDeleteSema(hold);

    PRINTF(signal, "%s, f: the signal", kind);
    PRINTF(waits_for_mutex, "%s, f: T waits for the mutex after the signal", kind);
    PRINTF(unlock, "%s, f: the main thread's unlock", kind);
    PRINTF(notify, "%s, f: the notify", kind);
    PRINTF(t_result, "%s, f: T's wait", kind);
    PRINTF(t_runs, "%s, f: runs of the callback when the wait returned", kind);
    PRINTF(t_unlock, "%s, f: T's unlock", kind);
    PRINTF(t_check, "%s, f: T's check", kind);
    PRINTF(t_check_runs, "%s, f: runs of the callback after the check", kind);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (lightweight = 0; lightweight < 2; lightweight++) {
        case_reads();
        case_unlock();
        case_locker();
        case_above();
    }
    lightweight = 1;
    case_handover();

    test_exit();
    return 0;
}
