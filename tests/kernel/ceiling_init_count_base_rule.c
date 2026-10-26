// Whether a thread at 0xA0 can create ceiling mutexes with each of several init counts and
// ceilings, and what ceiling the info of each one that is created reports. The main thread, on
// core 1, creates a mutex with an init count of 1 and each of the ceilings 0xB0, 0xA0 and 0x90,
// then one with an init count of 0 and the ceiling 0xB0. Each create prints its result and, if it
// made a mutex, the ceiling in the mutex's info. Each mutex is deleted right after its lines are
// printed, so every create starts with the main thread at the same priority. The cases:
// - an init count of 1 with the ceiling 0xB0, a lower priority than the main thread's
// - an init count of 1 with the ceiling 0xA0, the same priority as the main thread's
// - an init count of 1 with the ceiling 0x90, a higher priority than the main thread's
// - an init count of 0 with the ceiling 0xB0
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static const struct {
    int init_count, ceiling;
} cases[] = {
    { 1, 0xB0 },
    { 1, 0xA0 },
    { 1, 0x90 },
    { 0, 0xB0 },
};

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = cases[i].ceiling };
        const SceUID mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING,
                                                  cases[i].init_count, &opt);
        PRINTF(test_created(mutex), "init count %d, ceiling 0x%X: the create", cases[i].init_count,
               cases[i].ceiling);
        if (mutex < 0)
            continue;
        MutexInfo info = { .size = sizeof(info) };
        sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
        PRINTF(info.ceilingPriority, "init count %d, ceiling 0x%X: the ceiling in its info",
               cases[i].init_count, cases[i].ceiling);
        sceKernelDeleteMutex(mutex);
    }

    test_exit();
    return 0;
}
