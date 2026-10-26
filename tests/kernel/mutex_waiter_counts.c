// How a recursive mutex hands over to waiters that each asked for a count of their own. The main
// thread owns the mutex, the waiters queue for their counts, and a waiter that gets the mutex holds
// at a gate before each call it makes on it. After each step the test reads the owner, the count,
// the waiting threads and the wait type of the waiter still queued, while the waiter that got the
// mutex is blocked at its gate. The cases:
// - a, FIFO queue: W1 asks for 3 and W2 for 2. The main thread unlocks 1, so W1 gets 3, then W1
//   unlocks 1 (the count is 2 and W2 still waits) and then 2, so W2 gets 2 and unlocks it
// - b, the same with a priority queue (0x2000): W1 at a lower priority asks for 3 and W2 at a higher
//   one for 2, so W2 gets the mutex first and unlocks 1 twice, then W1 gets 3 and unlocks it
// - c, the main thread locks 5 in one call and W asks for 2: the main thread unlocks 2, then 2, then
//   2 again (0x80028144 with 1 held), then 1, so W gets 2
// - d, a helper locks 4 and ends while W1 asks for 2 and W2 for 3: the helper's end gives the mutex
//   to W1 with 2, and W1's unlock of 2 gives it to W2 with 3
// - e, W asks for 0x7FFFFFFF: its lock of 1 more overflows the count, and the main thread's try lock
//   fails while W owns the mutex
// The waiters are pinned to core 0 below the main thread, so a woken waiter stays ready until the
// main thread blocks. The gates keep each waiter from making its next call until the test lets it.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define HIGH_WAITER_PRIORITY 0xA0

#define MAX_WAITERS 2
#define MAX_STEPS 2

// A call a waiter makes after its gate opens, NO_CALL when the waiter has no more
enum { NO_CALL, LOCK_CALL, UNLOCK_CALL };

typedef struct Step {
    int call;
    int count;
} Step;

#define LOCK_STEP(n) ((Step){ LOCK_CALL, (n) })
#define UNLOCK_STEP(n) ((Step){ UNLOCK_CALL, (n) })
#define NO_STEP ((Step){ NO_CALL, 0 })

static SceUID mutex, go, helper, main_thread;
static SceUID threads[MAX_WAITERS], gates[MAX_WAITERS];
static int want[MAX_WAITERS];
static Step steps[MAX_WAITERS][MAX_STEPS];
static volatile int lock_result[MAX_WAITERS], step_result[MAX_WAITERS][MAX_STEPS];
static volatile int gate_count[MAX_WAITERS], helper_result;

// Asks for want[i], then makes each step once its gate opens, and counts each gate it reaches
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    lock_result[i] = sceKernelLockMutex(mutex, want[i], &timeout);
    for (int k = 0; k < MAX_STEPS && steps[i][k].call != NO_CALL && lock_result[i] == 0; k++) {
        gate_count[i] = k + 1;
        timeout = 3 * TEST_WAIT_US;
        sceKernelWaitSema(gates[i], 1, &timeout);
        timeout = 3 * TEST_WAIT_US;
        if (steps[i][k].call == LOCK_CALL)
            step_result[i][k] = sceKernelLockMutex(mutex, steps[i][k].count, &timeout);
        else
            step_result[i][k] = sceKernelUnlockMutex(mutex, steps[i][k].count);
    }
    return 0;
}

// Holds a lock for a count of 4 until go is signaled, then returns, so the mutex goes to its waiters
static int helper_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    helper_result = sceKernelLockMutex(mutex, 4, &timeout);
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static MutexInfo mutex_info(void) {
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    return info;
}

// Maps the owner to 0 for none, 1 for the main thread, 2 for W1, 3 for W2 and 4 for anything else
static int owner_code(SceUID owner) {
    if (owner == 0)
        return 0;
    if (owner == main_thread)
        return 1;
    for (int i = 0; i < MAX_WAITERS; i++) {
        if (owner == threads[i])
            return 2 + i;
    }
    return 4;
}

// Prints the owner, the count and the waiting threads, and the wait type of waiter queued, if any
static void print_state(const char *label, int queued) {
    const MutexInfo info = mutex_info();
    PRINTF(owner_code(info.currentOwnerId), "%s: the owner", label);
    PRINTF(info.currentCount, "%s: the count", label);
    PRINTF(info.numWaitThreads, "%s: the waiting threads", label);
    if (queued >= 0)
        PRINTF(wait_type_of(threads[queued]), "%s: W%d's wait type", label, queued + 1);
}

// Sets the count waiter i asks for and the two steps it makes after its gates
static void set_waiter(int i, int count, Step first, Step second) {
    want[i] = count;
    steps[i][0] = first;
    steps[i][1] = second;
}

// Starts waiter i at priority on core 0, and returns once it queues for the mutex
static void start_waiter(int i, int priority) {
    gate_count[i] = 0;
    threads[i] = sceKernelCreateThread("waiter", waiter, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    TEST_WAIT_FOR(test_in_wait(threads[i], WAIT_TYPE_MUTEX, mutex));
}

// Returns once waiter i is blocked at its gate number n
static void wait_at_gate(int i, int n) {
    TEST_WAIT_FOR(gate_count[i] == n && test_in_wait(threads[i], WAIT_TYPE_SEMA, gates[i]));
}

// Makes a fresh recursive mutex, or one with the given attributes, and its gates
static void create_case(SceUInt attr) {
    mutex = sceKernelCreateMutex("mutex", attr, 0, NULL);
    gates[0] = sceKernelCreateSema("gate 1", 0, 0, 1, NULL);
    gates[1] = sceKernelCreateSema("gate 2", 0, 0, 1, NULL);
    threads[0] = -1;
    threads[1] = -1;
}

static void delete_case(void) {
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(gates[0]);
    sceKernelDeleteSema(gates[1]);
}

// Case a: a FIFO queue, W1 asks for 3 and unlocks 1 and 2, W2 asks for 2 and unlocks 2
static void fifo_case(void) {
    create_case(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("a: the main thread's lock to a count of 1", sceKernelLockMutex(mutex, 1, &timeout));
    set_waiter(0, 3, UNLOCK_STEP(1), UNLOCK_STEP(2));
    set_waiter(1, 2, UNLOCK_STEP(2), NO_STEP);
    start_waiter(0, WAITER_PRIORITY);
    start_waiter(1, WAITER_PRIORITY);

    PRINT("a, the main thread unlocks 1: the unlock", sceKernelUnlockMutex(mutex, 1));
    wait_at_gate(0, 1);
    print_state("a, the main thread unlocked 1", 1);

    sceKernelSignalSema(gates[0], 1);
    wait_at_gate(0, 2);
    PRINT("a, W1 unlocks 1: the unlock", step_result[0][0]);
    print_state("a, W1 unlocked 1", 1);

    sceKernelSignalSema(gates[0], 1);
    test_join_thread(threads[0]);
    PRINT("a, W1 unlocks 2: the unlock", step_result[0][1]);
    wait_at_gate(1, 1);
    print_state("a, W1 unlocked 2, W2 holds it", -1);

    sceKernelSignalSema(gates[1], 1);
    test_join_thread(threads[1]);
    PRINT("a, W2 unlocks 2: the unlock", step_result[1][0]);
    print_state("a, W2 unlocked 2", -1);
    PRINT("a: W1's lock", lock_result[0]);
    PRINT("a: W2's lock", lock_result[1]);
    delete_case();
}

// Case b: a priority queue, W1 at a lower priority asks for 3, W2 at a higher one for 2
static void priority_case(void) {
    create_case(SCE_KERNEL_MUTEX_ATTR_RECURSIVE | SCE_KERNEL_ATTR_THREAD_PRIO);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("b: the main thread's lock to a count of 1", sceKernelLockMutex(mutex, 1, &timeout));
    set_waiter(0, 3, UNLOCK_STEP(3), NO_STEP);
    set_waiter(1, 2, UNLOCK_STEP(1), UNLOCK_STEP(1));
    start_waiter(0, WAITER_PRIORITY);
    start_waiter(1, HIGH_WAITER_PRIORITY);

    PRINT("b, the main thread unlocks 1: the unlock", sceKernelUnlockMutex(mutex, 1));
    wait_at_gate(1, 1);
    print_state("b, the main thread unlocked 1", 0);

    sceKernelSignalSema(gates[1], 1);
    wait_at_gate(1, 2);
    PRINT("b, W2 unlocks 1: the unlock", step_result[1][0]);
    print_state("b, W2 unlocked 1", 0);

    sceKernelSignalSema(gates[1], 1);
    test_join_thread(threads[1]);
    PRINT("b, W2 unlocks 1 again: the unlock", step_result[1][1]);
    wait_at_gate(0, 1);
    print_state("b, W2 unlocked 1 again, W1 holds it", -1);

    sceKernelSignalSema(gates[0], 1);
    test_join_thread(threads[0]);
    PRINT("b, W1 unlocks 3: the unlock", step_result[0][0]);
    print_state("b, W1 unlocked 3", -1);
    PRINT("b: W1's lock", lock_result[0]);
    PRINT("b: W2's lock", lock_result[1]);
    delete_case();
}

// Case c: the main thread takes 5 and unlocks 2, 2, 2 (which fails with 1 held) and then 1
static void partial_case(void) {
    create_case(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("c: the main thread's lock to a count of 5", sceKernelLockMutex(mutex, 5, &timeout));
    set_waiter(0, 2, UNLOCK_STEP(2), NO_STEP);
    start_waiter(0, WAITER_PRIORITY);

    PRINT("c, the main thread unlocks 2: the unlock", sceKernelUnlockMutex(mutex, 2));
    print_state("c, the main thread unlocked 2", 0);
    PRINT("c, the main thread unlocks 2 again: the unlock", sceKernelUnlockMutex(mutex, 2));
    print_state("c, the main thread unlocked 2 again", 0);
    PRINT("c, the main thread unlocks 2 with 1 held: the unlock", sceKernelUnlockMutex(mutex, 2));
    print_state("c, the failed unlock", 0);
    PRINT("c, the main thread unlocks 1: the unlock", sceKernelUnlockMutex(mutex, 1));
    wait_at_gate(0, 1);
    print_state("c, the mutex went to W1", -1);

    sceKernelSignalSema(gates[0], 1);
    test_join_thread(threads[0]);
    PRINT("c, W1 unlocks 2: the unlock", step_result[0][0]);
    print_state("c, W1 unlocked 2", -1);
    PRINT("c: W1's lock", lock_result[0]);
    delete_case();
}

// Case d: a helper owns the mutex for 4 and ends, with W1 asking for 2 and W2 for 3 queued behind it
static void helper_case(void) {
    create_case(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    helper = sceKernelCreateThread("helper", helper_thread, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(helper, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(helper, WAIT_TYPE_SEMA, go));
    PRINT("d: the helper's lock to a count of 4", helper_result);
    set_waiter(0, 2, UNLOCK_STEP(2), NO_STEP);
    set_waiter(1, 3, UNLOCK_STEP(3), NO_STEP);
    start_waiter(0, WAITER_PRIORITY);
    start_waiter(1, WAITER_PRIORITY);

    sceKernelSignalSema(go, 1);
    test_join_thread(helper);
    wait_at_gate(0, 1);
    print_state("d, the helper ended, W1 holds it", 1);

    sceKernelSignalSema(gates[0], 1);
    test_join_thread(threads[0]);
    PRINT("d, W1 unlocks 2: the unlock", step_result[0][0]);
    wait_at_gate(1, 1);
    print_state("d, W1 unlocked 2, W2 holds it", -1);

    sceKernelSignalSema(gates[1], 1);
    test_join_thread(threads[1]);
    PRINT("d, W2 unlocks 3: the unlock", step_result[1][0]);
    print_state("d, W2 unlocked 3", -1);
    PRINT("d: W1's lock", lock_result[0]);
    PRINT("d: W2's lock", lock_result[1]);
    sceKernelDeleteSema(go);
    delete_case();
}

// Case e: W asks for 0x7FFFFFFF, then its lock of 1 overflows, the main thread's try lock fails
static void overflow_case(void) {
    create_case(SCE_KERNEL_MUTEX_ATTR_RECURSIVE);
    SceUInt timeout = TEST_WAIT_US;
    PRINT("e: the main thread's lock to a count of 1", sceKernelLockMutex(mutex, 1, &timeout));
    set_waiter(0, 0x7FFFFFFF, LOCK_STEP(1), UNLOCK_STEP(0x7FFFFFFF));
    start_waiter(0, WAITER_PRIORITY);

    PRINT("e, the main thread unlocks 1: the unlock", sceKernelUnlockMutex(mutex, 1));
    wait_at_gate(0, 1);
    print_state("e, W1 got 0x7FFFFFFF", -1);

    sceKernelSignalSema(gates[0], 1);
    wait_at_gate(0, 2);
    PRINT("e, W1's lock of 1: the lock", step_result[0][0]);
    PRINT("e, the main thread's try lock of 1: the try lock", sceKernelTryLockMutex(mutex, 1));
    print_state("e, after the lock and the try lock", -1);

    sceKernelSignalSema(gates[0], 1);
    test_join_thread(threads[0]);
    PRINT("e, W1 unlocks 0x7FFFFFFF: the unlock", step_result[0][1]);
    print_state("e, W1 unlocked 0x7FFFFFFF", -1);
    PRINT("e: W1's lock", lock_result[0]);
    delete_case();
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);
    main_thread = sceKernelGetThreadId();

    fifo_case();
    priority_case();
    partial_case();
    helper_case();
    overflow_case();

    test_exit();
    return 0;
}
