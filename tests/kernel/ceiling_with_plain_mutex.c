// What a thread's priority is after each step of locking and unlocking a plain mutex together with
// mutexes with a ceiling. Owner O runs at 0xB0 on core 0, runs the steps of a case, and notes each
// step's result and its priority after it, while the main thread runs on core 1. The output calls
// a mutex by its ceiling, and P is the plain mutex. The cases:
// - lock P, lock 0x80, unlock 0x80, unlock P
// - lock 0x80, lock P, unlock 0x80, unlock P
// - lock P, lock 0x90, lock 0x80, unlock 0x80, unlock 0x90, unlock P
// - lock 0x80, set its priority to 0xB8, lock P, unlock P, unlock 0x80
// - O at 0x70: lock 0x80, lock P, set its priority to 0xB0, unlock 0x80, unlock P
//
// On a PS Vita TV on firmware 3.65 a plain mutex never changes the priority, and it
// does not hide a ceiling either, whatever the order of the locks. O gets 0x80 from the ceiling
// 0x80 and loses it when that mutex is unlocked, with P held or not. With 0x90 and 0x80 held it
// runs at 0x80, then 0x90 after the unlock of 0x80, and at 0xB0 after the unlock of 0x90 while P
// is still held. A priority lowered to 0xB8 under a ceiling is only stored, and O gets it once the
// ceiling mutex is unlocked, even though P was locked and unlocked in between. O at 0x70 that
// lowers itself to 0xB0 while holding 0x80 and P gets its base back, 0xB0, at the unlock of 0x80,
// although P is still held.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_STEPS 6
#define MUTEXES 3

enum Op { END, LOCK, UNLOCK, SET_PRIORITY };

typedef struct Step {
    int op, mutex, arg;
} Step;

typedef struct Case {
    const char *name;
    int owner_priority, ceilings[MUTEXES];
    Step steps[MAX_STEPS];
} Case;

// A ceiling of 0 is a plain mutex
static const Case cases[] = {
    { "lock P, lock 0x80, unlock 0x80, unlock P", 0xB0, { 0, 0x80, 0 },
      { { LOCK, 0, 0 }, { LOCK, 1, 0 }, { UNLOCK, 1, 0 }, { UNLOCK, 0, 0 }, { END, 0, 0 } } },
    { "lock 0x80, lock P, unlock 0x80, unlock P", 0xB0, { 0, 0x80, 0 },
      { { LOCK, 1, 0 }, { LOCK, 0, 0 }, { UNLOCK, 1, 0 }, { UNLOCK, 0, 0 }, { END, 0, 0 } } },
    { "lock P, lock 0x90, lock 0x80, unlock 0x80, unlock 0x90, unlock P", 0xB0, { 0, 0x90, 0x80 },
      { { LOCK, 0, 0 }, { LOCK, 1, 0 }, { LOCK, 2, 0 }, { UNLOCK, 2, 0 }, { UNLOCK, 1, 0 },
        { UNLOCK, 0, 0 } } },
    { "lock 0x80, set 0xB8, lock P, unlock P, unlock 0x80", 0xB0, { 0, 0x80, 0 },
      { { LOCK, 1, 0 }, { SET_PRIORITY, 0, 0xB8 }, { LOCK, 0, 0 }, { UNLOCK, 0, 0 },
        { UNLOCK, 1, 0 }, { END, 0, 0 } } },
    { "O at 0x70, lock 0x80, lock P, set 0xB0, unlock 0x80, unlock P", 0x70, { 0, 0x80, 0 },
      { { LOCK, 1, 0 }, { LOCK, 0, 0 }, { SET_PRIORITY, 0, 0xB0 }, { UNLOCK, 1, 0 },
        { UNLOCK, 0, 0 }, { END, 0, 0 } } },
};

static const char *const op_names[] = { "", "lock %s", "unlock %s", "set its priority to 0x%X" };

static const Case *current;
static SceUID mutexes[MUTEXES];
static int results[MAX_STEPS], priorities[MAX_STEPS];

static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int i = 0; i < MAX_STEPS && current->steps[i].op != END; i++) {
        const Step *step = &current->steps[i];
        const SceUID mutex = mutexes[step->mutex];
        if (step->op == LOCK)
            results[i] = sceKernelLockMutex(mutex, 1, NULL);
        else if (step->op == UNLOCK)
            results[i] = sceKernelUnlockMutex(mutex, 1);
        else
            results[i] = sceKernelChangeThreadPriority(0, step->arg);
        priorities[i] = sceKernelGetThreadCurrentPriority();
    }
    return 0;
}

static void run_case(const Case *c) {
    current = c;
    for (int i = 0; i < MUTEXES; i++) {
        mutexes[i] = -1;
        if (i == 0) {
            mutexes[i] = sceKernelCreateMutex("plain", 0, 0, NULL);
        } else if (c->ceilings[i]) {
            SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = c->ceilings[i] };
            mutexes[i] = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
        }
    }
    for (int i = 0; i < MAX_STEPS; i++)
        results[i] = priorities[i] = -1;
    const SceUID owner = sceKernelCreateThread("owner", owner_entry, c->owner_priority, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    test_join_thread(owner);
    for (int i = 0; i < MUTEXES; i++) {
        if (mutexes[i] >= 0)
            sceKernelDeleteMutex(mutexes[i]);
    }

    for (int i = 0; i < MAX_STEPS && c->steps[i].op != END; i++) {
        const Step *step = &c->steps[i];
        char step_name[40], mutex_name[8];
        if (step->mutex == 0)
            sceClibSnprintf(mutex_name, sizeof(mutex_name), "P");
        else
            sceClibSnprintf(mutex_name, sizeof(mutex_name), "0x%X", c->ceilings[step->mutex]);
        if (step->op == SET_PRIORITY)
            sceClibSnprintf(step_name, sizeof(step_name), op_names[step->op], step->arg);
        else
            sceClibSnprintf(step_name, sizeof(step_name), op_names[step->op], mutex_name);
        PRINTF(results[i], "%s, step %d, %s: the result", c->name, i + 1, step_name);
        PRINTF(priorities[i], "%s, step %d, %s: the priority after it", c->name, i + 1, step_name);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);
    test_exit();
    return 0;
}
