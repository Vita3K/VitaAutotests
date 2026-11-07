// Which priority the ceiling of a mutex created locked is checked against when its creator runs
// above its own priority. The main thread, on core 1 at 0xB0, locks a mutex with the ceiling 0x70,
// which runs it at 0x70, then creates mutexes with an init count of 1 and each of the ceilings 0x60,
// 0x80 and 0, then one with an init count of 0 and the ceiling 0x80. Each create prints its result,
// the ceiling in the new mutex's info, and the main thread's priority after it. Each new mutex is
// unlocked and deleted before the next create. The cases:
// - the ceiling 0x60, above both 0x70 and 0xB0
// - the ceiling 0x80, below 0x70 but above 0xB0
// - the ceiling 0, which stands for the creator's priority
// - an init count of 0 with the ceiling 0x80
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static const struct {
    int init_count, ceiling;
} cases[] = {
    { 1, 0x60 },
    { 1, 0x80 },
    { 1, 0 },
    { 0, 0x80 },
};

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, 0xB0);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    SceKernelMutexOptParam raise = { .size = sizeof(raise), .ceilingPriority = 0x70 };
    const SceUID raiser = sceKernelCreateMutex("raiser", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &raise);
    PRINT("lock the mutex with the ceiling 0x70", sceKernelLockMutex(raiser, 1, NULL));
    PRINT("the main thread's priority", sceKernelGetThreadCurrentPriority());

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
        PRINTF(sceKernelGetThreadCurrentPriority(), "init count %d, ceiling 0x%X: the priority after it",
               cases[i].init_count, cases[i].ceiling);
        if (cases[i].init_count > 0)
            sceKernelUnlockMutex(mutex, cases[i].init_count);
        sceKernelDeleteMutex(mutex);
    }

    PRINT("unlock the mutex with the ceiling 0x70", sceKernelUnlockMutex(raiser, 1));
    PRINT("the main thread's priority after it", sceKernelGetThreadCurrentPriority());
    sceKernelDeleteMutex(raiser);

    test_exit();
    return 0;
}
