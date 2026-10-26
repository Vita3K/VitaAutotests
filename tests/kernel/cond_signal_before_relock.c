// Whether a condition waiter that a signal wakes is in the mutex's queue as soon as the signal
// wakes it, or only once it runs and relocks. The mutex is priority ordered, and a plain locker L
// waits for it too, so the order the two get it in depends on whether W is queued when main
// unlocks, and on the priority W has when it joins. Main holds the mutex and starts W on the
// condition and L on the mutex, then signals W. Cases:
// - a: nothing else, so the unlock finds only L queued if W hasn't joined the queue yet
// - b: the same, but main waits for W to report its relock (wait type 0x200) before the unlock
// - c: W at 0xB0 and L at 0xA8. Main raises W to 0x90 after the signal, then waits for its report
// - d: W at 0xA0 and L at 0xA8. Main lowers W to 0xB8 after the signal, then waits for its report
// Every thread is pinned to core 0 and main runs at 0x60. After each signal and each unlock, main
// spins for HOLD_US with no blocking call, so a thread that the signal or the unlock wakes can't
// run until main blocks. The test relies on that, and the spin is long enough for a thread that
// runs at once to finish its relock. Each case reads W's state and the mutex's owner after its
// spin, before it joins, and prints after both threads have ended. The order is printed as hex
// digits, one per acquisition: W is 1 and L is 2, so 0x21 means L got the mutex first.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define HOLD_US 1000000

#define DIGIT_W 1
#define DIGIT_L 2

static SceUID mutex, cond, waiter, locker;
static volatile int w_result, l_result, order;

// Spins for HOLD_US with no blocking call
static void hold_core(void) {
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    while (sceKernelGetProcessTimeWide() - start < HOLD_US)
        ;
}

// Locks the mutex, waits on the condition, then records its turn and unlocks
static int cond_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt timeout = TEST_WAIT_US;
    w_result = sceKernelWaitCond(cond, &timeout);
    if (w_result == 0) {
        order = (order << 4) | DIGIT_W;
        sceKernelUnlockMutex(mutex, 1);
    }
    return 0;
}

// Locks the mutex, records its turn and unlocks. Its timeout is past every hand-over of a case, so
// it gets the mutex after W when W goes first.
static int locker_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    l_result = sceKernelLockMutex(mutex, 1, &timeout);
    if (l_result == 0) {
        order = (order << 4) | DIGIT_L;
        sceKernelUnlockMutex(mutex, 1);
    }
    return 0;
}

// What main reads of W, the condition and the mutex before it unlocks
typedef struct State {
    int waiting;
    int ready;
    SceUInt32 wait_type;
    int cond_waiters;
    int mutex_waiters;
} State;

static State read_state(void) {
    State state = { 0 };
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(waiter, &info);
    state.waiting = (info.status & SCE_THREAD_WAITING) != 0;
    state.ready = (info.status & (SCE_THREAD_READY | SCE_THREAD_STANDBY)) != 0;
    state.wait_type = info.waitType;
    SceKernelCondInfo cond_info = { .size = sizeof(cond_info) };
    sceKernelGetCondInfo(cond, &cond_info);
    state.cond_waiters = cond_info.numWaitThreads;
    MutexInfo mutex_info = { .size = sizeof(mutex_info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&mutex_info);
    state.mutex_waiters = mutex_info.numWaitThreads;
    return state;
}

static SceUID mutex_owner(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info.currentOwnerId;
}

static void print_state(const char *when, const State *state) {
    PRINTF(state->waiting, "%s: W is in a wait", when);
    PRINTF(state->ready, "%s: W is ready or on standby", when);
    PRINTF(state->wait_type, "%s: W's wait type", when);
    PRINTF(state->cond_waiters, "%s: the condition's waiters", when);
    PRINTF(state->mutex_waiters, "%s: the mutex's waiters", when);
}

// Prints what a case got once both threads ended
static void print_end(const char *name, SceUID owner) {
    PRINTF(owner == waiter, "%s: W owns the mutex after the unlock", name);
    PRINTF(owner == locker, "%s: L owns the mutex after the unlock", name);
    PRINTF(order, "%s: the order the mutex went in", name);
    PRINTF(w_result, "%s: W's wait result", name);
    PRINTF(l_result, "%s: L's lock result", name);
}

// Creates a priority ordered mutex and a first in, first out condition on it
static void create(void) {
    mutex = sceKernelCreateMutex("mutex", SCE_KERNEL_ATTR_THREAD_PRIO, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    order = 0;
    w_result = l_result = 0xDEAD;
}

static void destroy(void) {
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(mutex);
}

// Starts W at w_prio, and waits until it waits on the condition. Main then locks the mutex, and L
// starts at l_prio and waits for it.
static void start_threads(SceUInt32 w_prio, SceUInt32 l_prio) {
    waiter = sceKernelCreateThread("waiter", cond_waiter, w_prio, 0x4000, 0,
                                   SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND, cond));
    sceKernelLockMutex(mutex, 1, NULL);
    locker = sceKernelCreateThread("locker", locker_thread, l_prio, 0x4000, 0,
                                   SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(locker, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(locker, WAIT_TYPE_MUTEX, mutex));
}

// Waits up to 4 s for a thread to end and deletes it. A relock has no timeout, so a thread that is
// still running ends the test with a marker line, instead of hanging it.
static void join_or_exit(SceUID thread) {
    SceUInt timeout = 2 * TEST_WAIT_US;
    if (sceKernelWaitThreadEnd(thread, NULL, &timeout) < 0) {
        PRINT("a thread did not end within 4 s", 1);
        test_exit();
    }
    sceKernelDeleteThread(thread);
}

static void join_threads(void) {
    join_or_exit(waiter);
    join_or_exit(locker);
}

// Nothing else: main unlocks after the signal, so L is the only thread queued unless W has already
// joined the queue
static void signal_then_unlock(void) {
    create();
    start_threads(0xA0, 0xB0);
    sceKernelSignalCond(cond);
    hold_core();
    const State signaled = read_state();
    sceKernelUnlockMutex(mutex, 1);
    hold_core();
    const SceUID owner = mutex_owner();
    join_threads();
    print_state("a, after the signal", &signaled);
    print_end("a", owner);
    destroy();
}

// The same, but main waits for W to report its relock before the unlock
static void report_then_unlock(void) {
    create();
    start_threads(0xA0, 0xB0);
    sceKernelSignalCond(cond);
    hold_core();
    const State signaled = read_state();
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND_MUTEX, mutex));
    const State reported = read_state();
    sceKernelUnlockMutex(mutex, 1);
    hold_core();
    const SceUID owner = mutex_owner();
    join_threads();
    print_state("b, after the signal", &signaled);
    print_state("b, after the report", &reported);
    print_end("b", owner);
    destroy();
}

// Main raises W above L while W is ready, then waits for W's report before the unlock
static void raise_then_unlock(void) {
    create();
    start_threads(0xB0, 0xA8);
    sceKernelSignalCond(cond);
    hold_core();
    const State signaled = read_state();
    const int raised = sceKernelChangeThreadPriority(waiter, 0x90);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND_MUTEX, mutex));
    const State reported = read_state();
    sceKernelUnlockMutex(mutex, 1);
    hold_core();
    const SceUID owner = mutex_owner();
    join_threads();
    print_state("c, after the signal", &signaled);
    PRINTF(raised, "c: the raise of W");
    print_state("c, after the report", &reported);
    print_end("c", owner);
    destroy();
}

// Main lowers W below L while W is ready, then waits for W's report before the unlock
static void lower_then_unlock(void) {
    create();
    start_threads(0xA0, 0xA8);
    sceKernelSignalCond(cond);
    hold_core();
    const State signaled = read_state();
    const int lowered = sceKernelChangeThreadPriority(waiter, 0xB8);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_COND_MUTEX, mutex));
    const State reported = read_state();
    sceKernelUnlockMutex(mutex, 1);
    hold_core();
    const SceUID owner = mutex_owner();
    join_threads();
    print_state("d, after the signal", &signaled);
    PRINTF(lowered, "d: the lower of W");
    print_state("d, after the report", &reported);
    print_end("d", owner);
    destroy();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    signal_then_unlock();
    report_then_unlock();
    raise_then_unlock();
    lower_then_unlock();

    test_exit();
    return 0;
}
