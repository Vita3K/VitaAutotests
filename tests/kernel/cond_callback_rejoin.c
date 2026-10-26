// Where a condition variable waiter rejoins its condition after the callback it ran during its
// wait, and what a timeout that passes during that callback does. Each case runs for a heavy
// condition (sceKernelCreateCond) and for a lightweight one (sceKernelCreateLwCond), with fresh
// objects and fresh threads. Every thread is pinned to core 0. The main thread runs at 0x60 and
// the waiters at 0xB0, so a waiter the main thread wakes stays ready until the main thread blocks.
// The callback of the waiter T blocks on a gate semaphore, so the main thread changes the
// condition while T is out of it. The waiters record their turn while they hold the mutex, then
// unlock it. The cases:
// - a FIFO condition. T waits first with callbacks, then W2 waits behind it. T's callback is
//   notified and blocks on the gate, W3 queues behind W2, and the gate opens. Three single signals
//   wake the waiters in the order they reach the mutex, printed as hex digits with T as 1.
// - a priority condition with all waiters at 0xB0. As above, but during T's callback the main
//   thread raises T to 0x80 in one run and lowers it to 0xB8 in another.
// - a wait of 500 ms whose timeout passes during the callback. The callback blocks on the gate
//   until 750 ms after the notify, then the gate opens. The wait result, whether T owns the mutex,
//   its time left and the runs of the callback are printed, and whether T ended within 250 ms of
//   the gate. Then the same, with the main thread holding the mutex through the gate, so T has to
//   lock the mutex again after its timeout passed. Its wait type is printed 250 ms after the gate,
//   and it returns only after the main thread unlocks the mutex. This case runs last.
// - sceKernelSignalCondAll while T's callback blocks. T isn't among the waiters, and after the
//   gate opens T waits on the condition again.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_SET 0xDEAD
// The timeout of the waits that the main thread ends by a signal, far longer than any step
#define LONG_TIMEOUT (4 * TEST_WAIT_US)
#define SHORT_TIMEOUT_US 500000
// How long after the notify the gate opens, past the short timeout
#define GATE_US 750000
// How long the test lets a state settle before it reads it, or waits to see a wait stay
#define SETTLE_US 250000
#define MAX_WITHIN_US 250000

static int lightweight;
static SceUID mutex, cond, gate, callback;
static SceKernelLwMutexWork *lw_mutex;
static SceKernelLwCondWork *lw_cond;
static SceUInt wait_timeout;

static volatile int callback_started, callback_done, callback_runs, callback_priority;
static volatile int order_count, order[3], results[3], finished[3];
static volatile int t_left, t_owns, t_runs;

// Blocks on the gate until the main thread opens it
static int rejoin_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_started = 1;
    SceUInt timeout = LONG_TIMEOUT;
    sceKernelWaitSema(gate, 1, &timeout);
    callback_priority = sceKernelGetThreadCurrentPriority();
    callback_runs++;
    callback_done = 1;
    return 0;
}

static void lock_mutex(void) {
    if (lightweight)
        sceKernelLockLwMutex(lw_mutex, 1, NULL);
    else
        sceKernelLockMutex(mutex, 1, NULL);
}

static void unlock_mutex(void) {
    if (lightweight)
        sceKernelUnlockLwMutex(lw_mutex, 1);
    else
        sceKernelUnlockMutex(mutex, 1);
}

// Returns the owner of the mutex, or -1
static SceUID mutex_owner(void) {
    if (lightweight) {
        LwMutexInfo info = { .size = sizeof(info) };
        return sceKernelGetLwMutexInfo(lw_mutex, &info) == 0 ? info.currentOwnerId : -1;
    }
    MutexInfo info = { .size = sizeof(info) };
    if (sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info) < 0)
        return -1;
    return info.currentOwnerId;
}

// Returns the UID of the condition, which a waiter reports as its wait ID
static SceUID cond_id(void) {
    if (lightweight) {
        LwCondInfo info = { .size = sizeof(info) };
        return sceKernelGetLwCondInfo(lw_cond, &info) == 0 ? info.uid : -1;
    }
    return cond;
}

// Returns the wait type of a waiter on the condition, with callbacks for the first turn
static SceUInt32 cond_wait_type(int turn) {
    const SceUInt32 type = lightweight ? WAIT_TYPE_LW_COND : WAIT_TYPE_COND;
    return turn == 1 ? type | WAIT_TYPE_CB : type;
}

static int cond_wait(int turn, SceUInt *timeout) {
    if (turn == 1) {
        if (lightweight)
            return sceKernelWaitLwCondCB(lw_cond, timeout);
        return sceKernelWaitCondCB(cond, timeout);
    }
    if (lightweight)
        return sceKernelWaitLwCond(lw_cond, timeout);
    return sceKernelWaitCond(cond, timeout);
}

static int cond_signal(void) {
    return lightweight ? sceKernelSignalLwCond(lw_cond) : sceKernelSignalCond(cond);
}

static int cond_signal_all(void) {
    return lightweight ? sceKernelSignalLwCondAll(lw_cond) : sceKernelSignalCondAll(cond);
}

// Waits on the condition with its timeout. Turn 1 uses callbacks and records its turn, the time
// left and whether it owns the mutex after its wait. The others only record their turn.
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int turn = *(const int *)argp;
    if (turn == 1)
        callback = sceKernelCreateCallback("rejoin", 0, rejoin_callback, NULL);
    lock_mutex();
    SceUInt timeout = wait_timeout;
    results[turn - 1] = cond_wait(turn, &timeout);
    if (turn == 1) {
        t_left = timeout;
        t_owns = mutex_owner() == sceKernelGetThreadId();
        t_runs = callback_runs;
    }
    order[order_count++] = turn;
    unlock_mutex();
    if (turn == 1)
        sceKernelDeleteCallback(callback);
    finished[turn - 1] = 1;
    return 0;
}

static SceUID start_waiter(int turn, SceUInt32 priority) {
    const SceUID thread = sceKernelCreateThread("waiter", waiter, priority, 0x4000, 0,
                                                SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(turn), &turn);
    return thread;
}

// Resets what the threads record for a case
static void reset(void) {
    callback_started = callback_done = callback_runs = 0;
    callback_priority = NOT_SET;
    order_count = 0;
    t_left = t_owns = t_runs = NOT_SET;
    for (int i = 0; i < 3; i++) {
        order[i] = 0;
        results[i] = NOT_SET;
        finished[i] = 0;
    }
}

// Creates the gate, the mutex and the condition of a case. The lightweight objects are not deleted.
static void begin(SceUInt32 cond_attr, SceKernelLwMutexWork *m, SceKernelLwCondWork *c) {
    lw_mutex = m;
    lw_cond = c;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    if (lightweight) {
        sceKernelCreateLwMutex(m, "lw mutex", SCE_KERNEL_ATTR_THREAD_PRIO, 0, NULL);
        sceKernelCreateLwCond(c, "lw cond", cond_attr, m, NULL);
    } else {
        mutex = sceKernelCreateMutex("mutex", SCE_KERNEL_ATTR_THREAD_PRIO, 0, NULL);
        cond = sceKernelCreateCond("cond", cond_attr, mutex, NULL);
    }
    reset();
}

// Deletes the gate, and the heavy objects of the case
static void finish(void) {
    sceKernelDeleteSema(gate);
    if (!lightweight) {
        sceKernelDeleteCond(cond);
        sceKernelDeleteMutex(mutex);
    }
}

// The sum of the turns the waiters got the mutex in, as hex digits
static int turn_digits(void) {
    return order[0] * 0x100 + order[1] * 0x10 + order[2];
}

// T waits with callbacks, W2 and W3 wait behind it. T's callback blocks on the gate while W3
// queues, and change_to, if not 0, is the priority the main thread gives T during the callback.
static void order_case(const char *name, int light, SceUInt32 cond_attr, SceUInt32 change_to) {
    lightweight = light;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    begin(cond_attr, &m, &c);
    wait_timeout = LONG_TIMEOUT;

    const SceUID t = start_waiter(1, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(t, cond_wait_type(1), cond_id()));
    const SceUID w2 = start_waiter(2, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(w2, cond_wait_type(2), cond_id()));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_started);
    const SceUID w3 = start_waiter(3, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(w3, cond_wait_type(2), cond_id()));
    int changed = NOT_SET;
    if (change_to)
        changed = sceKernelChangeThreadPriority(t, change_to);
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(callback_done && test_in_wait(t, cond_wait_type(1), cond_id()));
    const int waits_again = test_in_wait(t, cond_wait_type(1), cond_id());

    for (int k = 1; k <= 3; k++) {
        cond_signal();
        TEST_WAIT_FOR(order_count == k);
    }
    test_join_thread(t);
    test_join_thread(w2);
    test_join_thread(w3);

    if (change_to)
        PRINTF(changed, "%s: the priority change", name);
    PRINTF(waits_again, "%s: T waits on the condition again after the callback", name);
    PRINTF(callback_priority, "%s: T's priority at the end of the callback", name);
    PRINTF(turn_digits(), "%s: the order the waiters get the mutex in", name);
    PRINTF(results[0], "%s: T's result", name);
    PRINTF(results[1], "%s: W2's result", name);
    PRINTF(results[2], "%s: W3's result", name);
    finish();
}

// T's wait of 500 ms, whose timeout passes during its callback. With hold, the main thread holds
// the mutex through the gate, so T waits to lock it again after its timeout.
static void timeout_case(const char *name, int light, int hold) {
    lightweight = light;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    begin(0, &m, &c);
    wait_timeout = SHORT_TIMEOUT_US;

    const SceUID t = start_waiter(1, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(t, cond_wait_type(1), cond_id()));
    const SceUInt64 notified = sceKernelGetProcessTimeWide();
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_started);
    if (hold)
        lock_mutex();
    while (sceKernelGetProcessTimeWide() < notified + GATE_US)
        sceKernelDelayThread(1000);
    sceKernelSignalSema(gate, 1);

    int ended = 0, wait_type = NOT_SET, returned_before_unlock = NOT_SET;
    if (hold) {
        sceKernelDelayThread(SETTLE_US);
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(t, &info);
        wait_type = info.waitType;
        returned_before_unlock = finished[0];
        unlock_mutex();
        TEST_WAIT_FOR(finished[0]);
        if (finished[0])
            test_join_thread(t);
    } else {
        SceUInt within = MAX_WITHIN_US;
        ended = sceKernelWaitThreadEnd(t, NULL, &within) == 0;
        test_join_thread(t);
    }

    if (hold) {
        PRINTF(wait_type, "%s: T's wait type 250 ms after the gate", name);
        PRINTF(returned_before_unlock, "%s: T returned before the unlock", name);
    } else {
        PRINTF(ended, "%s: T ended within 250 ms of the gate", name);
    }
    PRINTF(results[0], "%s: T's result", name);
    PRINTF(t_owns, "%s: T owns the mutex", name);
    PRINTF(t_left == 0, "%s: T's time left is 0", name);
    PRINTF(t_runs, "%s: runs of the callback when the wait returned", name);
    finish();
}

// The main thread signals all waiters while T's callback blocks, so T isn't one of them
static void signal_all_case(const char *name, int light) {
    lightweight = light;
    SceKernelLwMutexWork m;
    SceKernelLwCondWork c;
    begin(0, &m, &c);
    wait_timeout = LONG_TIMEOUT;

    const SceUID t = start_waiter(1, WAITER_PRIORITY);
    TEST_WAIT_FOR(test_in_wait(t, cond_wait_type(1), cond_id()));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(callback_started);
    const int signal_all = cond_signal_all();
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(callback_done && test_in_wait(t, cond_wait_type(1), cond_id()));
    sceKernelDelayThread(SETTLE_US);
    const int waits_after = test_in_wait(t, cond_wait_type(1), cond_id());
    cond_signal();
    test_join_thread(t);

    PRINTF(signal_all, "%s: the signal to all", name);
    PRINTF(waits_after, "%s: T still waits on the condition after the gate", name);
    PRINTF(results[0], "%s: T's result", name);
    PRINTF(t_owns, "%s: T owns the mutex after its wait", name);
    finish();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    order_case("heavy, FIFO condition", 0, 0, 0);
    order_case("lightweight, FIFO condition", 1, 0, 0);
    order_case("heavy, priority condition, T raised to 0x80 during its callback", 0,
               SCE_KERNEL_ATTR_THREAD_PRIO, 0x80);
    order_case("lightweight, priority condition, T raised to 0x80 during its callback", 1,
               SCE_KERNEL_ATTR_THREAD_PRIO, 0x80);
    order_case("heavy, priority condition, T lowered to 0xB8 during its callback", 0,
               SCE_KERNEL_ATTR_THREAD_PRIO, 0xB8);
    order_case("lightweight, priority condition, T lowered to 0xB8 during its callback", 1,
               SCE_KERNEL_ATTR_THREAD_PRIO, 0xB8);
    timeout_case("heavy, timeout passes during the callback", 0, 0);
    timeout_case("lightweight, timeout passes during the callback", 1, 0);
    signal_all_case("heavy, signal to all while the callback waits", 0);
    signal_all_case("lightweight, signal to all while the callback waits", 1);
    timeout_case("heavy, timeout passes during the callback, mutex held through the gate", 0, 1);
    timeout_case("lightweight, timeout passes during the callback, held through the gate", 1, 1);

    test_exit();
    return 0;
}
