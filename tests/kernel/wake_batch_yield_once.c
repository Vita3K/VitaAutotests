// The order in which threads that one call wakes at once run afterwards, when the waker sits
// between them in priority. Three waiters and the waker M are pinned to core 0. A at 0x80 and B
// at 0x70 are above M at 0x90, and C at 0xA0 is below it. The waiters queue in the order A, B, C,
// which is not their priority order. Each call wakes all three waiters: a semaphore signal of 3, a
// semaphore cancel and a semaphore delete, an event flag set with the multi attribute, a manual
// reset simple event set, a rwlock write unlock to three readers, and a mutex delete. Each run
// records one hex digit per thread as it runs, first to run first, with A as 0, B as 1, C as 2
// and M as 3. M records its digit right after the call returns. The run order prints as four
// digits. The first waiter to run also reads the other two, and the test prints whether both were
// ready with no wait, and whether that first waiter is B, the highest priority waiter. Every
// result is read after the run, since printing lets the other threads run.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x90
#define WAITERS 3
#define WAKER 3
#define HIGHEST 1
#define MAX_RUNS 4

typedef enum Kind { SEMA, FLAG, MUTEX, RWLOCK, EVENT } Kind;
typedef enum Call { RELEASE, CANCEL, DELETE } Call;

// Priorities of the waiters A, B and C
static const int priorities[WAITERS] = { 0x80, 0x70, 0xA0 };

static Kind kind;
static SceUID object, threads[WAITERS];
static volatile int run_count, first_saw_ready;
static int order[MAX_RUNS];
static volatile int result[WAITERS];

// Whether every waiter other than who is ready to run, with no wait left
static int others_ready(int who) {
    for (int i = 0; i < WAITERS; i++) {
        if (i == who)
            continue;
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(threads[i], &info);
        const int ready = info.status == SCE_THREAD_READY || info.status == SCE_THREAD_STANDBY;
        if (info.waitType != 0 || !ready)
            return 0;
    }
    return 1;
}

// Records that who ran, and what the first thread to run saw of the other waiters
static void ran(int who) {
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    if (n < MAX_RUNS)
        order[n] = who;
    if (n == 0 && who < WAITERS)
        first_saw_ready = others_ready(who);
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    SceUInt32 bits;
    switch (kind) {
    case SEMA: result[i] = sceKernelWaitSema(object, 1, &timeout); break;
    case FLAG:
        result[i] = sceKernelWaitEventFlag(object, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
        break;
    case MUTEX: result[i] = sceKernelLockMutex(object, 1, &timeout); break;
    case RWLOCK: result[i] = sceKernelLockReadRWLock(object, &timeout); break;
    case EVENT: result[i] = sceKernelWaitEvent(object, EVENT_BIT_A, &bits, NULL, &timeout); break;
    }
    ran(i);
    return 0;
}

static void create(Kind k) {
    switch (k) {
    case SEMA: object = sceKernelCreateSema("sema", 0, 0, WAITERS, NULL); break;
    case FLAG: object = sceKernelCreateEventFlag("flag", SCE_EVENT_WAITMULTIPLE, 0, NULL); break;
    case MUTEX: object = sceKernelCreateMutex("mutex", 0, 1, NULL); break;
    case RWLOCK:
        object = sceKernelCreateRWLock("rw", 0, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case EVENT:
        object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_MANUAL_RESET, 0, NULL);
        break;
    }
}

static int call(Kind k, Call c) {
    int waiting;
    switch (k) {
    case SEMA:
        if (c == RELEASE)
            return sceKernelSignalSema(object, 3);
        if (c == CANCEL)
            return sceKernelCancelSema(object, 0, &waiting);
        return sceKernelDeleteSema(object);
    case FLAG: return sceKernelSetEventFlag(object, 0x1);
    case MUTEX: return sceKernelDeleteMutex(object);
    case RWLOCK: return sceKernelUnlockWriteRWLock(object);
    case EVENT: return sceKernelSetEvent(object, EVENT_BIT_A, 0);
    }
    return 0;
}

static void cleanup(Kind k, Call c) {
    if (c == DELETE)
        return;
    switch (k) {
    case SEMA: sceKernelDeleteSema(object); break;
    case FLAG: sceKernelDeleteEventFlag(object); break;
    case MUTEX: break;
    case RWLOCK:
        // The readers that got the lock ended without unlocking it
        sceKernelDeleteRWLock(object);
        break;
    case EVENT: sceKernelDeleteSimpleEvent(object); break;
    }
}

static void run(Kind k, Call c, const char *name) {
    kind = k;
    run_count = 0;
    first_saw_ready = 0;
    for (int i = 0; i < MAX_RUNS; i++)
        order[i] = 0xF;
    for (int i = 0; i < WAITERS; i++)
        result[i] = 0xDEAD;
    create(k);
    for (int i = 0; i < WAITERS; i++) {
        threads[i] = sceKernelCreateThread("waiter", waiter, priorities[i], 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[i], sizeof(i), &i);
        test_wait_until_waiting(threads[i]);
    }
    const int r = call(k, c);
    ran(WAKER);
    // Waits until every thread has recorded its run, then joins them
    TEST_WAIT_FOR(run_count == MAX_RUNS);
    for (int i = 0; i < WAITERS; i++)
        test_join_thread(threads[i]);
    cleanup(k, c);
    const int run_order = (order[0] << 12) | (order[1] << 8) | (order[2] << 4) | order[3];
    PRINTF(r, "%s: the call", name);
    PRINTF(result[0], "%s: A's result", name);
    PRINTF(result[1], "%s: B's result", name);
    PRINTF(result[2], "%s: C's result", name);
    PRINTF(run_order, "%s: run order", name);
    PRINTF(first_saw_ready, "%s: the first to run saw both others ready", name);
    PRINTF(order[0] == HIGHEST, "%s: the first to run was B", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run(SEMA, RELEASE, "semaphore signal of 3");
    run(SEMA, CANCEL, "semaphore cancel");
    run(SEMA, DELETE, "semaphore delete");
    run(FLAG, RELEASE, "event flag set, multi attribute");
    run(EVENT, RELEASE, "manual reset simple event set");
    run(RWLOCK, RELEASE, "rwlock write unlock to three readers");
    run(MUTEX, DELETE, "mutex delete");

    test_exit();
    return 0;
}
