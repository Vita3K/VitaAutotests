// Mutual exclusion and hand overs of a lightweight mutex under contention. Three threads, each
// pinned to one user core, take the same mutex in a loop. In each section a thread reads a shared
// counter and writes it back one higher, with no call in between, and checks a flag that says
// whether another thread is inside. Each run uses a fresh mutex, and the threads of a run are
// joined before the test reads anything. The test runs two phases: a FIFO mutex, where every
// section takes a count of 1, and a recursive mutex with the priority queue attribute, where every
// section takes a count of 1 and then a count of 2, and unlocks the 2 before the 1.
//
// The cases:
// - three threads, each with 3000 sections, on a FIFO mutex
// - three threads, each with 3000 sections with nested counts of 1 then 2, on a recursive mutex
//   with the priority queue attribute
//
// Every lock has a timeout of 10 s, and a thread that fails a lock or an unlock stops, so a broken
// mutex ends the run after a few timeouts. The test prints comparisons only. The wait count word is
// not printed at the end, since a stale value there is allowed.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define WORKERS 3
#define ITERATIONS 3000
#define COUNTER_END (WORKERS * ITERATIONS)

// How long a worker waits for the mutex. Long, so only a broken mutex times out.
#define LOCK_TIMEOUT_US 10000000

static SceKernelLwMutexWork mutex;
static volatile int counter, in_section, overlaps, failed_locks, failed_unlocks;
static int nested;

// Takes the mutex ITERATIONS times, and in each section increments the shared counter
static int worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < ITERATIONS; i++) {
        SceUInt32 timeout = LOCK_TIMEOUT_US;
        if (sceKernelLockLwMutex(&mutex, 1, &timeout) != 0) {
            failed_locks++;
            break;
        }
        if (nested) {
            timeout = LOCK_TIMEOUT_US;
            if (sceKernelLockLwMutex(&mutex, 2, &timeout) != 0) {
                failed_locks++;
                break;
            }
        }
        // The occupancy changes are atomic, so two workers inside at once always count an overlap
        if (__atomic_add_fetch(&in_section, 1, __ATOMIC_SEQ_CST) != 1)
            __atomic_add_fetch(&overlaps, 1, __ATOMIC_SEQ_CST);
        const int seen = counter;
        counter = seen + 1;
        __atomic_sub_fetch(&in_section, 1, __ATOMIC_SEQ_CST);
        if (nested && sceKernelUnlockLwMutex(&mutex, 2) != 0) {
            failed_unlocks++;
            break;
        }
        if (sceKernelUnlockLwMutex(&mutex, 1) != 0) {
            failed_unlocks++;
            break;
        }
    }
    return 0;
}

// Runs the workers on a fresh mutex made with attr, one per user core, then prints what they left
static void run_phase(const char *name, SceUInt32 attr) {
    static const SceUInt32 masks[WORKERS] = {
        SCE_KERNEL_CPU_MASK_USER_0,
        SCE_KERNEL_CPU_MASK_USER_1,
        SCE_KERNEL_CPU_MASK_USER_2,
    };
    counter = 0;
    in_section = 0;
    overlaps = 0;
    failed_locks = 0;
    failed_unlocks = 0;
    sceKernelCreateLwMutex(&mutex, "contention", attr, 0, NULL);
    SceUID workers[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        workers[i] = sceKernelCreateThread(
            "worker", worker, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, masks[i], NULL);
        sceKernelStartThread(workers[i], 0, NULL);
    }
    for (int i = 0; i < WORKERS; i++)
        test_join_thread(workers[i]);

    PRINTF(counter == COUNTER_END, "%s: the counter ends at %d", name, COUNTER_END);
    PRINTF(failed_locks == 0, "%s: no lock failed", name);
    PRINTF(failed_unlocks == 0, "%s: no unlock failed", name);
    PRINTF(overlaps == 0, "%s: never two inside the section", name);

    const volatile unsigned int *words = (const volatile unsigned int *)&mutex;
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&mutex, &info);
    PRINTF(words[0] == 0, "%s: word 0 is 0", name);
    PRINTF(words[2] == 0, "%s: word 2 is 0", name);
    PRINTF(info.currentOwnerId == 0, "%s: no owner", name);
    PRINTF(info.currentCount == 0, "%s: the count is 0", name);
    PRINTF(info.numWaitThreads == 0, "%s: no waiters", name);
    sceKernelDeleteLwMutex(&mutex);
}

int main(void) {
    run_phase("FIFO", 0);
    nested = 1;
    run_phase("recursive, priority queue", LW_MUTEX_ATTR_RECURSIVE | SCE_KERNEL_ATTR_THREAD_PRIO);
    test_exit();
    return 0;
}
