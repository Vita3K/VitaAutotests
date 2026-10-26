// What the priority of a thread is through three nested mutexes with ceilings, locked in one order
// and unlocked in the reverse. Owner O, at 0xB0 on core 0, locks A (ceiling 0x90), B (ceiling 0x70)
// and C (ceiling 0x80), then unlocks C, B and A, and notes each step's result and its priority
// after it. The main thread, on core 1, prints them once O has ended. The output calls each mutex
// by its ceiling. One case: the steps are lock, lock, lock, unlock, unlock and unlock, so each
// priority after an unlock shows which of the mutexes still held sets it.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OWNER_PRIORITY 0xB0
#define MUTEXES 3
#define MAX_STEPS 6

enum Op { LOCK, UNLOCK };

typedef struct Step {
    int op, mutex;
} Step;

// The ceilings of the mutexes A, B and C
static const int ceilings[MUTEXES] = { 0x90, 0x70, 0x80 };
static const Step steps[MAX_STEPS] = {
    { LOCK, 0 }, { LOCK, 1 }, { LOCK, 2 }, { UNLOCK, 2 }, { UNLOCK, 1 }, { UNLOCK, 0 },
};
static const char *const op_names[] = { "lock", "unlock" };

static SceUID mutexes[MUTEXES];
static int results[MAX_STEPS], priorities[MAX_STEPS];

// Runs each step on its mutex, and notes its result and the owner's priority after it
static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < MAX_STEPS; i++) {
        const Step *step = &steps[i];
        const SceUID mutex = mutexes[step->mutex];
        if (step->op == LOCK)
            results[i] = sceKernelLockMutex(mutex, 1, NULL);
        else
            results[i] = sceKernelUnlockMutex(mutex, 1);
        priorities[i] = sceKernelGetThreadCurrentPriority();
    }
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int i = 0; i < MUTEXES; i++) {
        SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = ceilings[i] };
        mutexes[i] = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    }
    for (int i = 0; i < MAX_STEPS; i++)
        results[i] = priorities[i] = -1;
    const SceUID owner = sceKernelCreateThread("owner", owner_entry, OWNER_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    test_join_thread(owner);

    for (int i = 0; i < MAX_STEPS; i++) {
        const Step *step = &steps[i];
        const int ceiling = ceilings[step->mutex];
        PRINTF(results[i], "three nested ceilings, step %d, %s 0x%X: the result", i + 1,
               op_names[step->op], ceiling);
        PRINTF(priorities[i], "three nested ceilings, step %d, %s 0x%X: the priority after it",
               i + 1, op_names[step->op], ceiling);
    }
    for (int i = 0; i < MUTEXES; i++)
        sceKernelDeleteMutex(mutexes[i]);

    test_exit();
    return 0;
}
