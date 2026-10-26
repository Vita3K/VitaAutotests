// Whether a call that doesn't wait gives up the core to a thread of the same priority that is ready
// behind the caller. The thread O waits on a semaphore, and the thread L runs on core 0 too. The
// main thread, on core 1, starts O, waits until it is waiting, starts L and joins both. L records 0
// and signals O, so O is ready behind L. L then spins without syscalls until O has run, or for a
// fixed number of loops, makes its call, records 2, and waits for O to end. O records 1 when it runs
// and signals L. The steps should be 0 2 1, since the call doesn't yield and O only runs once L
// waits. The spin only makes sure that a woken thread which runs before its waker's next line has
// recorded its step before the call. Each case runs at 0x70 and at 0xA0, with objects of its own,
// and the calls are:
// - sceKernelLockMutex and sceKernelUnlockMutex on a mutex nobody holds
// - sceKernelLockLwMutex and sceKernelUnlockLwMutex on a lightweight mutex nobody holds
// - sceKernelLockReadRWLock and sceKernelUnlockReadRWLock
// - sceKernelLockWriteRWLock and sceKernelUnlockWriteRWLock
// - sceKernelSignalSema, sceKernelSetEventFlag and sceKernelSetEvent with no waiter
// - sceKernelSignalCond and sceKernelSignalLwCond with no waiter
// - sceKernelStartTimer and sceKernelStopTimer
// - sceKernelCancelSema with no waiter
// - sceKernelCreateSema and sceKernelDeleteSema
// - sceKernelCreateThread and sceKernelDeleteThread of a dormant thread
// - sceKernelStartThread of a dormant thread of a lower priority
// - sceKernelChangeThreadPriority of a dormant thread of a lower priority
// - sceKernelGetThreadInfo of L itself
// - sceKernelCheckCallback with nothing pending
//
// Each case prints the result of each call and the steps in the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_STEPS 4
// Loops of the spin at most, enough for a woken thread to run on another core before the call
#define SPINS 20000000
// The lower priority threads of a case are this far below the caller, and their priority is
// changed to twice this far
#define LOWER 0x8

typedef enum Call {
    LOCK_MUTEX,
    LOCK_LW_MUTEX,
    LOCK_READ_RWLOCK,
    LOCK_WRITE_RWLOCK,
    SIGNAL_SEMA,
    SET_EVENT_FLAG,
    SET_EVENT,
    SIGNAL_COND,
    SIGNAL_LW_COND,
    START_STOP_TIMER,
    CANCEL_SEMA,
    CREATE_DELETE_SEMA,
    CREATE_DELETE_THREAD,
    START_LOWER_THREAD,
    CHANGE_LOWER_PRIORITY,
    GET_THREAD_INFO,
    CHECK_CALLBACK,
} Call;

static const char *const call_names[] = {
    "sceKernelLockMutex and sceKernelUnlockMutex",
    "sceKernelLockLwMutex and sceKernelUnlockLwMutex",
    "sceKernelLockReadRWLock and sceKernelUnlockReadRWLock",
    "sceKernelLockWriteRWLock and sceKernelUnlockWriteRWLock",
    "sceKernelSignalSema with no waiter",
    "sceKernelSetEventFlag with no waiter",
    "sceKernelSetEvent with no waiter",
    "sceKernelSignalCond with no waiter",
    "sceKernelSignalLwCond with no waiter",
    "sceKernelStartTimer and sceKernelStopTimer",
    "sceKernelCancelSema with no waiter",
    "sceKernelCreateSema and sceKernelDeleteSema",
    "sceKernelCreateThread and sceKernelDeleteThread of a dormant thread",
    "sceKernelStartThread of a dormant lower priority thread",
    "sceKernelChangeThreadPriority of a dormant lower priority thread",
    "sceKernelGetThreadInfo of the caller",
    "sceKernelCheckCallback with nothing pending",
};

static SceUID go_sema, done_sema, free_sema, flag, event, cond_mutex, cond, timer, mutex, rwlock;
static SceUID lower, low, other;
static SceKernelLwMutexWork lw_mutex, lw_cond_mutex;
static SceKernelLwCondWork lw_cond;
static volatile int other_ran;
static int case_priority, paired, call_result, call_result2;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits until L signals it, records 1, then signals L
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    record(1);
    other_ran = 1;
    sceKernelSignalSema(done_sema, 1);
    return 0;
}

// Does nothing, for the lower priority threads
static int idle_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Makes the call of the case, and for the pairs the call that undoes it
static void perform(Call call) {
    SceUInt timeout = TEST_WAIT_US;
    int num_waiters = 0;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    paired = 0;
    switch (call) {
    case LOCK_MUTEX:
        call_result = sceKernelLockMutex(mutex, 1, &timeout);
        call_result2 = sceKernelUnlockMutex(mutex, 1);
        paired = 1;
        break;
    case LOCK_LW_MUTEX:
        call_result = sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
        call_result2 = sceKernelUnlockLwMutex(&lw_mutex, 1);
        paired = 1;
        break;
    case LOCK_READ_RWLOCK:
        call_result = sceKernelLockReadRWLock(rwlock, &timeout);
        call_result2 = sceKernelUnlockReadRWLock(rwlock);
        paired = 1;
        break;
    case LOCK_WRITE_RWLOCK:
        call_result = sceKernelLockWriteRWLock(rwlock, &timeout);
        call_result2 = sceKernelUnlockWriteRWLock(rwlock);
        paired = 1;
        break;
    case SIGNAL_SEMA:
        call_result = sceKernelSignalSema(free_sema, 1);
        break;
    case SET_EVENT_FLAG:
        call_result = sceKernelSetEventFlag(flag, 0x1);
        break;
    case SET_EVENT:
        call_result = sceKernelSetEvent(event, EVENT_BIT_A, 0);
        break;
    case SIGNAL_COND:
        call_result = sceKernelSignalCond(cond);
        break;
    case SIGNAL_LW_COND:
        call_result = sceKernelSignalLwCond(&lw_cond);
        break;
    case START_STOP_TIMER:
        call_result = sceKernelStartTimer(timer);
        call_result2 = sceKernelStopTimer(timer);
        paired = 1;
        break;
    case CANCEL_SEMA:
        call_result = sceKernelCancelSema(free_sema, 0, &num_waiters);
        break;
    case CREATE_DELETE_SEMA: {
        const SceUID sema = sceKernelCreateSema("tmp", 0, 0, 1, NULL);
        call_result = test_created(sema);
        call_result2 = sceKernelDeleteSema(sema);
        paired = 1;
        break;
    }
    case CREATE_DELETE_THREAD: {
        const SceUID thread = sceKernelCreateThread("tmp", idle_entry, case_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        call_result = test_created(thread);
        call_result2 = sceKernelDeleteThread(thread);
        paired = 1;
        break;
    }
    case START_LOWER_THREAD:
        call_result = sceKernelStartThread(lower, 0, NULL);
        break;
    case CHANGE_LOWER_PRIORITY:
        call_result = sceKernelChangeThreadPriority(lower, case_priority + 2 * LOWER);
        break;
    case GET_THREAD_INFO:
        call_result = sceKernelGetThreadInfo(low, &info);
        break;
    case CHECK_CALLBACK:
        call_result = sceKernelCheckCallback();
        break;
    }
}

// Records 0, readies O, spins until O ran, makes the call, records 2, then waits for O to finish
static int low_entry(SceSize args, void *argp) {
    (void)args;
    const Call call = *(const Call *)argp;
    record(0);
    sceKernelSignalSema(go_sema, 1);
    for (volatile int n = 0; n < SPINS && !other_ran; n++)
        ;
    perform(call);
    record(2);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(done_sema, 1, &timeout);
    return 0;
}

// Waits for a thread to end, for a few seconds at most, and deletes it
static void join(SceUID thread) {
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

// Creates the objects that the calls use, so that a call only makes its own
static void create_objects(int priority) {
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);
    done_sema = sceKernelCreateSema("done", 0, 0, 1, NULL);
    free_sema = sceKernelCreateSema("free", 0, 0, 1, NULL);
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
    sceKernelCreateLwMutex(&lw_cond_mutex, "lw cond mutex", 0, 0, NULL);
    sceKernelCreateLwCond(&lw_cond, "lw cond", 0, &lw_cond_mutex, NULL);
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    timer = sceKernelCreateTimer("timer", 0, NULL);
    lower = sceKernelCreateThread("lower", idle_entry, priority + LOWER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
}

static void delete_objects(void) {
    sceKernelDeleteTimer(timer);
    sceKernelDeleteCond(cond);
    sceKernelDeleteLwCond(&lw_cond);
    sceKernelDeleteLwMutex(&lw_cond_mutex);
    sceKernelDeleteMutex(cond_mutex);
    sceKernelDeleteLwMutex(&lw_mutex);
    sceKernelDeleteRWLock(rwlock);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteEventFlag(flag);
    sceKernelDeleteSimpleEvent(event);
    sceKernelDeleteSema(free_sema);
    sceKernelDeleteSema(go_sema);
    sceKernelDeleteSema(done_sema);
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

// Runs one call from L at the priority, with O ready behind it, and prints the results and the steps
static void run_case(Call call, int priority) {
    char name[160];
    sceClibSnprintf(name, sizeof(name), "%s at 0x%X", call_names[call], priority);
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    call_result = call_result2 = -1;
    other_ran = 0;
    case_priority = priority;
    create_objects(priority);

    other = sceKernelCreateThread("other", other_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(other, 0, NULL);
    test_wait_until_waiting(other);
    low = sceKernelCreateThread("low", low_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(low, sizeof(call), &call);

    // Joined first, so every record the threads made is complete
    join(low);
    join(other);
    // The lower priority thread started by the call runs once the threads above it have ended
    if (call == START_LOWER_THREAD && call_result == 0)
        join(lower);
    else
        sceKernelDeleteThread(lower);
    delete_objects();

    if (paired) {
        PRINTF(call_result, "%s: the first call", name);
        PRINTF(call_result2, "%s: the second call", name);
    } else {
        PRINTF(call_result, "%s: the call", name);
    }
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int call = LOCK_MUTEX; call <= CHECK_CALLBACK; call++)
        run_case((Call)call, 0x70);
    for (int call = LOCK_MUTEX; call <= CHECK_CALLBACK; call++)
        run_case((Call)call, 0xA0);

    test_exit();
    return 0;
}
