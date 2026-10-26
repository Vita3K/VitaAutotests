// What creating a ceiling mutex with an init count does to the thread that creates it. The main
// thread, at 0xA0 on core 1, creates a mutex with the ceiling 0x80 and an init count of 1, and
// prints the create's result, its own priority after the create, the count in the mutex's info and
// whether it owns the mutex. It then unlocks the mutex once and prints the unlock's result and its
// priority after the unlock. The control creates the same mutex with an init count of 0, which
// starts free, and prints the same lines. The mutex is deleted after its lines are printed. The
// cases:
// - an init count of 1
// - an init count of 0, the control
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CEILING 0x80

// Creates a mutex with the init count, prints what the create did to this thread, then unlocks the
// mutex once if it was created and deletes it
static void init_case(const char *name, int init_count) {
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    const SceUID mutex =
        sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, init_count, &opt);
    PRINTF(test_created(mutex), "%s: the create", name);
    PRINTF(sceKernelGetThreadCurrentPriority(), "%s: the main thread's priority after the create",
           name);
    if (mutex < 0)
        return;

    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    PRINTF(info.currentCount, "%s: the count in its info", name);
    PRINTF(info.currentOwnerId == sceKernelGetThreadId(), "%s: the main thread owns it", name);
    PRINTF(sceKernelUnlockMutex(mutex, 1), "%s: the unlock", name);
    PRINTF(sceKernelGetThreadCurrentPriority(), "%s: the main thread's priority after the unlock",
           name);
    sceKernelDeleteMutex(mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    init_case("init count 1", 1);
    init_case("init count 0 (control)", 0);

    test_exit();
    return 0;
}
