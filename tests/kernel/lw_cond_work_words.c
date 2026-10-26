// What the work area of a lightweight mutex holds during a lightweight condition variable wait, at
// the steps of a wait with the mutex held, free, recursive, given to a relocking waiter, and with
// a callback notified during the relock. Word 0 is the owner, word 1 the wait count and word 2 the
// lock count. Word 0 is compared with the main thread's UID, the waiter's UID and 0, and words 1
// and 2 are printed as numbers. The steps, each in its own case with fresh objects:
// - the waiter holds the mutex once, blocked at a gate
// - the waiter waits on the condition variable with the mutex free
// - a recursive mutex, held twice by the waiter, then the waiter waits on the condition variable
//   with it, then the waiter relocks it after a signal
// - the main thread holds the mutex and signals, so the waiter waits for the mutex with the wait
//   type 0x400000, then the main thread unlocks, with the waiter ready, then the waiter's wait
//   returned
// - a callback notified while the waiter relocks, so the waiter is out of the queue and blocked in
//   its callback, with the main thread unlocking the mutex
// The words are read before any print, since a print sleeps, and while the waiter is blocked in a
// wait or at a gate, except right after the main thread's unlock, where the waiter is ready. The
// words can't change before the gate that follows the waiter's wait, so that read is stable too. The waiter is pinned to
// core 0 below the main thread, so it stays ready until the main thread blocks. Lightweight mutexes
// are never deleted here.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define COND_US 4000000
#define GATE_US 4000000

static SceKernelLwMutexWork mutex;
static SceKernelLwCondWork cond;
static SceUID hold_gate, after_gate, callback_gate, callback;
static SceUID main_thread, waiter_thread;
static volatile int wait_result, unlocks;
// What the waiter does, set by each case before it starts
static int locks, use_cond, use_callbacks, hold_before_wait, gate_after_wait;

// The wait type of thread, without its callback bit, or 0 if it isn't waiting
static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(thread, &info) < 0 || !(info.status & SCE_THREAD_WAITING))
        return 0;
    return info.waitType & ~WAIT_TYPE_CB;
}

// Blocks on gate until the main thread signals it, for at most GATE_US
static void pass_gate(SceUID gate) {
    SceUInt timeout = GATE_US;
    sceKernelWaitSema(gate, 1, &timeout);
}

// Locks the mutex once, for at most TEST_WAIT_US
static void lock_mutex(void) {
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockLwMutex(&mutex, 1, &timeout);
}

static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    pass_gate(callback_gate);
    return 0;
}

// Runs the steps of the case, then unlocks the mutex until it isn't owned, and counts the unlocks
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (use_callbacks)
        callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
    for (int i = 0; i < locks; i++)
        lock_mutex();
    if (hold_before_wait)
        pass_gate(hold_gate);
    if (use_cond) {
        SceUInt timeout = COND_US;
        wait_result = use_callbacks ? sceKernelWaitLwCondCB(&cond, &timeout)
                                    : sceKernelWaitLwCond(&cond, &timeout);
        if (gate_after_wait)
            pass_gate(after_gate);
    }
    while (sceKernelUnlockLwMutex(&mutex, 1) == 0)
        unlocks++;
    if (use_callbacks)
        sceKernelDeleteCallback(callback);
    return 0;
}

// Reads the words of the work area, then prints word 0 compared with the main thread, the waiter
// and 0, and words 1 and 2 as numbers
static void print_words(const char *step) {
    const unsigned int owner = ((const volatile unsigned int *)&mutex)[0];
    const unsigned int waiters = ((const volatile unsigned int *)&mutex)[1];
    const unsigned int count = ((const volatile unsigned int *)&mutex)[2];
    PRINTF(owner == (unsigned int)main_thread, "%s: word 0 is the main thread", step);
    PRINTF(owner == (unsigned int)waiter_thread, "%s: word 0 is the waiter", step);
    PRINTF(owner == 0, "%s: word 0 is 0", step);
    PRINTF(waiters, "%s: word 1", step);
    PRINTF(count, "%s: word 2", step);
}

// Creates a fresh lightweight mutex, recursive if asked, and a condition variable on it, with the
// gates the waiter blocks at
static void reset(int recursive) {
    sceKernelCreateLwMutex(&mutex, "mutex", recursive ? LW_MUTEX_ATTR_RECURSIVE : 0, 0, NULL);
    sceKernelCreateLwCond(&cond, "cond", 0, &mutex, NULL);
    hold_gate = sceKernelCreateSema("hold gate", 0, 0, 1, NULL);
    after_gate = sceKernelCreateSema("after gate", 0, 0, 1, NULL);
    callback_gate = sceKernelCreateSema("callback gate", 0, 0, 1, NULL);
    wait_result = 0xDEAD;
    unlocks = 0;
}

static void start_waiter(void) {
    waiter_thread = sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0,
        SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
}

// Joins the waiter, then prints its wait result and unlocks, and deletes the condition variable and
// the gates. The lightweight mutex stays.
static void finish(const char *name) {
    test_join_thread(waiter_thread);
    if (use_cond)
        PRINTF(wait_result, "%s: the waiter's wait", name);
    PRINTF(unlocks, "%s: the waiter's unlocks after it", name);
    sceKernelDeleteLwCond(&cond);
    sceKernelDeleteSema(hold_gate);
    sceKernelDeleteSema(after_gate);
    sceKernelDeleteSema(callback_gate);
}

// The waiter holds the mutex once, blocked at a gate
static void case_holds_once(void) {
    reset(0);
    locks = 1;
    use_cond = 0;
    use_callbacks = 0;
    hold_before_wait = 1;
    gate_after_wait = 0;
    start_waiter();
    TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, hold_gate));
    print_words("the waiter holds the mutex once");
    sceKernelSignalSema(hold_gate, 1);
    finish("the waiter holds the mutex once");
}

// The waiter waits on the condition variable while the mutex is free
static void case_waits_mutex_free(void) {
    reset(0);
    locks = 1;
    use_cond = 1;
    use_callbacks = 0;
    hold_before_wait = 0;
    gate_after_wait = 0;
    start_waiter();
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND);
    print_words("the waiter waits on the condition variable, the mutex free");
    sceKernelSignalLwCond(&cond);
    finish("the waiter waits on the condition variable, the mutex free");
}

// A recursive mutex, held twice by the waiter: while it holds it, while it waits on the condition
// variable, and after the signal and the relock, with the waiter blocked at a gate
static void case_recursive(void) {
    reset(1);
    locks = 2;
    use_cond = 1;
    use_callbacks = 0;
    hold_before_wait = 1;
    gate_after_wait = 1;
    start_waiter();
    TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, hold_gate));
    print_words("recursive, the waiter holds it twice");
    sceKernelSignalSema(hold_gate, 1);
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND);
    print_words("recursive, the waiter waits on the condition variable");
    sceKernelSignalLwCond(&cond);
    TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, after_gate));
    print_words("recursive, after the waiter's wait returned");
    sceKernelSignalSema(after_gate, 1);
    finish("recursive");
}

// The main thread holds the mutex and signals, so the waiter waits for it with the wait type
// 0x400000. The main thread then unlocks, which hands the mutex to the waiter, and the waiter
// blocks at a gate after its wait.
static void case_main_holds(void) {
    reset(0);
    locks = 1;
    use_cond = 1;
    use_callbacks = 0;
    hold_before_wait = 0;
    gate_after_wait = 1;
    start_waiter();
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND);
    lock_mutex();
    sceKernelSignalLwCond(&cond);
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND_MUTEX);
    print_words("the main thread holds the mutex, the waiter is signaled");
    const int unlock = sceKernelUnlockLwMutex(&mutex, 1);
    print_words("right after the main thread's unlock, the waiter ready");
    PRINT("the main thread's unlock", unlock);
    TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, after_gate));
    print_words("the waiter's wait returned, it holds the mutex");
    sceKernelSignalSema(after_gate, 1);
    finish("the main thread holds the mutex and signals");
}

// A callback notified while the waiter relocks: the waiter leaves the mutex's queue and blocks
// in its callback, and the main thread unlocks the mutex meanwhile
static void case_notify_relock(void) {
    reset(0);
    locks = 1;
    use_cond = 1;
    use_callbacks = 1;
    hold_before_wait = 0;
    gate_after_wait = 0;
    start_waiter();
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND);
    lock_mutex();
    sceKernelSignalLwCond(&cond);
    TEST_WAIT_FOR(wait_type_of(waiter_thread) == WAIT_TYPE_LW_COND_MUTEX);
    const int notify = sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, callback_gate));
    print_words("notified while the waiter relocks, the waiter in its callback");
    const int unlock = sceKernelUnlockLwMutex(&mutex, 1);
    PRINT("the notify", notify);
    PRINT("the main thread's unlock", unlock);
    sceKernelSignalSema(callback_gate, 1);
    finish("notified while the waiter relocks");
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    main_thread = sceKernelGetThreadId();

    case_holds_once();
    case_waits_mutex_free();
    case_recursive();
    case_main_holds();
    case_notify_relock();

    test_exit();
    return 0;
}
