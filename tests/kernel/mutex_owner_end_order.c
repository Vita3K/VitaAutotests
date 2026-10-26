// The lock results of the waiters of several locks when their owner thread ends while it still holds
// all of them. Each case has one waiter per lock, and each waiter is queued on its lock before the
// owner ends, so the end of the owner hands every lock to its waiter. Waiter 0 waits on the first
// lock of the case, waiter 1 on the second, and so on. Main, the owner and the waiters run on core 0
// at the priorities main 0x60, owner 0xA0 and waiters 0xB0, so the waiters are pinned below the owner
// and main. The cases:
// - a: mutexes A, B and C, created in that order, and the owner locks B, C, then A
// - b: the same three mutexes, and the owner locks C, B, then A
// - c: a recursive mutex A and a mutex B, and the owner locks A, B, then A again
// - d: the owner creates mutex N with an initial count of 1, then locks mutex M, which main created
// - e: rwlock R is write locked by the owner, then mutex M is locked
// - f: rwlocks R1 and R2 are created in that order, and the owner write locks R2, then R1
// Each case prints the order the waiters ran in, one hex digit per waiter index, first to run first,
// and each waiter's lock result. Every wait has a timeout, so a case that doesn't release its
// waiters still ends.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define OWNER_PRIORITY 0xA0
#define WAITER_PRIORITY 0xB0
#define MAX_LOCKS 3
#define NOT_SET 0xDEAD

// What a lock is. A recursive mutex is locked twice by the owner in case c.
enum Kind { MUTEX, RECURSIVE_MUTEX, WRITE_LOCK };

// A case: its locks, the kind and the name of each, the lock the owner creates itself with an
// initial count of 1 (-1 for none), and the sequence of locks the owner takes, by index.
typedef struct Case {
    const char *name;
    int count;
    const char *on[MAX_LOCKS];
    int kinds[MAX_LOCKS];
    int creates;
    int steps;
    int order[MAX_LOCKS];
} Case;

static const Case cases[] = {
    { "mutexes A, B, C, the owner locks B, C, A", 3, { "A", "B", "C" }, { MUTEX, MUTEX, MUTEX }, -1, 3, { 1, 2, 0 } },
    { "mutexes A, B, C, the owner locks C, B, A", 3, { "A", "B", "C" }, { MUTEX, MUTEX, MUTEX }, -1, 3, { 2, 1, 0 } },
    { "recursive A and B, the owner locks A, B, A", 2, { "A", "B" }, { RECURSIVE_MUTEX, MUTEX }, -1, 3, { 0, 1, 0 } },
    { "the owner creates N with count 1, then locks M", 2, { "N", "M" }, { MUTEX, MUTEX }, 0, 1, { 1 } },
    { "rwlock R write locked, then mutex M locked", 2, { "R", "M" }, { WRITE_LOCK, MUTEX }, -1, 2, { 0, 1 } },
    { "rwlocks R1, R2, the owner write locks R2, R1", 2, { "R1", "R2" }, { WRITE_LOCK, WRITE_LOCK }, -1, 2, { 1, 0 } },
};

static const Case *current;
static SceUID locks[MAX_LOCKS];
static SceUID go;
static volatile int result[MAX_LOCKS];
static volatile int run_count;
static int order[MAX_LOCKS];

// Records that waiter i ran after its lock call returned
static void ran(int i) {
    const int n = __atomic_fetch_add(&run_count, 1, __ATOMIC_SEQ_CST);
    if (n < MAX_LOCKS)
        order[n] = i;
}

// Creates a lock of the given kind with no count
static SceUID create(int kind) {
    if (kind == WRITE_LOCK)
        return sceKernelCreateRWLock("rwlock", 0, NULL);
    return sceKernelCreateMutex("mutex", kind == RECURSIVE_MUTEX ? SCE_KERNEL_MUTEX_ATTR_RECURSIVE : 0, 0, NULL);
}

// Locks id of the given kind once, waiting at most timeout
static int take(SceUID id, int kind, SceUInt *timeout) {
    if (kind == WRITE_LOCK)
        return sceKernelLockWriteRWLock(id, timeout);
    return sceKernelLockMutex(id, 1, timeout);
}

// Gives back one lock of id's kind
static void give(SceUID id, int kind) {
    if (kind == WRITE_LOCK)
        sceKernelUnlockWriteRWLock(id);
    else
        sceKernelUnlockMutex(id, 1);
}

// Deletes a lock of the given kind
static void remove_lock(SceUID id, int kind) {
    if (kind == WRITE_LOCK)
        sceKernelDeleteRWLock(id);
    else
        sceKernelDeleteMutex(id);
}

// The wait type a thread reports while it waits for a lock of the given kind
static SceUInt32 wait_type(int kind) {
    return kind == WRITE_LOCK ? WAIT_TYPE_RW_LOCK : WAIT_TYPE_MUTEX;
}

// Waits for its lock, records its result, then gives the lock back if it got it
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    result[i] = take(locks[i], current->kinds[i], &timeout);
    ran(i);
    if (result[i] == 0)
        give(locks[i], current->kinds[i]);
    return 0;
}

// Creates the lock the case has the owner create, takes the locks in the case's order, then waits
// for go and ends without giving any of them back
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (current->creates >= 0)
        locks[current->creates] = sceKernelCreateMutex("mutex", 0, 1, NULL);
    for (int s = 0; s < current->steps; s++) {
        SceUInt timeout = TEST_WAIT_US;
        const int i = current->order[s];
        take(locks[i], current->kinds[i], &timeout);
    }
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

// Runs one case, then prints the lock result of each waiter
static void run(const Case *c) {
    current = c;
    run_count = 0;
    for (int i = 0; i < MAX_LOCKS; i++) {
        result[i] = NOT_SET;
        order[i] = 0xF;
    }
    for (int i = 0; i < c->count; i++)
        locks[i] = i == c->creates ? -1 : create(c->kinds[i]);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);

    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, go));

    SceUID waiters[MAX_LOCKS];
    for (int i = 0; i < c->count; i++) {
        waiters[i] = sceKernelCreateThread("waiter", waiter_entry, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(waiters[i], sizeof(i), &i);
        TEST_WAIT_FOR(test_in_wait(waiters[i], wait_type(c->kinds[i]), locks[i]));
    }

    // Only now the owner ends, which hands every lock to its waiter
    sceKernelSignalSema(go, 1);
    for (int i = 0; i < c->count; i++)
        test_join_thread(waiters[i]);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(owner, NULL, &timeout);
    sceKernelDeleteThread(owner);
    for (int i = 0; i < c->count; i++)
        remove_lock(locks[i], c->kinds[i]);
    sceKernelDeleteSema(go);

    int digits = 0;
    for (int i = 0; i < c->count; i++)
        digits = (digits << 4) | order[i];
    PRINTF(digits, "%s: run order", c->name);
    for (int i = 0; i < c->count; i++)
        PRINTF(result[i], "%s: the lock result of the waiter on %s", c->name, c->on[i]);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run(&cases[i]);

    test_exit();
    return 0;
}
