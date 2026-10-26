// What priority 0 means to sceKernelCreateThread and to a child's init and current priority while
// the creating thread holds a mutex with a ceiling. The main thread at 0xA0 creates a thread with
// priority 0 before anything else, which is the baseline. Then it locks a mutex with the ceiling
// 0x70, which raises it to 0x70, creates a thread with priority 0, sets its own base priority to
// 0x90, which is below the ceiling, creates another, unlocks the mutex and creates a third. Each
// child is joined before the next create. Each child reports its init priority and current priority
// from its info before it starts, and the priority it runs at. Also the result of the base change.
//
// This is the main thread case of ceiling_base_change, with a child created at each step, and the
// priority 0 case of thread_create_current_priority, here from a thread that holds a ceiling. The
// main thread is pinned to core 1 and the children run one at a time, since each is joined before
// the next create.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define CEILING 0x70
#define BASE_PRIORITY 0x90

static volatile int seen_priority;

static int report_priority(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    seen_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

// Creates a thread with priority 0, prints its init and current priority, then runs it and joins it
static void create_with_zero(const char *name) {
    const SceUID thread = sceKernelCreateThread("zero", report_priority, 0, 0x4000, 0, 0, NULL);
    PRINTF(test_created(thread), "%s: create with priority 0", name);
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINTF(info.initPriority, "%s: its initial priority", name);
    PRINTF(info.currentPriority, "%s: its current priority", name);
    seen_priority = -1;
    sceKernelStartThread(thread, 0, NULL);
    test_join_thread(thread);
    PRINTF(seen_priority, "%s: the priority it runs at", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    create_with_zero("the main thread at 0xA0");

    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    const SceUID mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    PRINT("lock the mutex with the ceiling 0x70", sceKernelLockMutex(mutex, 1, NULL));
    create_with_zero("the main thread holding the ceiling");

    PRINT("change the base priority to 0x90", sceKernelChangeThreadPriority(0, BASE_PRIORITY));
    create_with_zero("the main thread holding the ceiling, base 0x90");

    PRINT("unlock the mutex", sceKernelUnlockMutex(mutex, 1));
    create_with_zero("the main thread after the unlock, base 0x90");

    sceKernelDeleteMutex(mutex);
    sceKernelChangeThreadPriority(0, SCE_KERNEL_DEFAULT_PRIORITY_USER);
    test_exit();
    return 0;
}
