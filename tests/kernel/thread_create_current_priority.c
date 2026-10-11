// Creating a thread with priority 0, which stands for the current priority, from threads at
// different priorities
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int seen_priority;

static int report_priority(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    seen_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

// Creates a thread with priority 0 and prints whether it worked and the priority it got
static void create_with_zero(const char *from) {
    const SceUID thread = sceKernelCreateThread("zero", report_priority, 0, 0x4000, 0, 0, NULL);
    PRINTF(test_created(thread), "create with priority 0 from %s", from);
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINTF(info.initPriority, "its initial priority in the info, from %s", from);
    PRINTF(info.currentPriority, "its current priority in the info, from %s", from);
    seen_priority = -1;
    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);
    PRINTF(seen_priority, "the priority it runs at, from %s", from);
}

static int creator(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    create_with_zero("a thread at 0x70");
    return 0;
}

int main(void) {
    PRINT("the main thread's priority", sceKernelGetThreadCurrentPriority());
    create_with_zero("the main thread");

    PRINT("change the main thread to 0x50", sceKernelChangeThreadPriority(0, 0x50));
    create_with_zero("the main thread at 0x50");

    const SceUID thread = sceKernelCreateThread("creator", creator, 0x70, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);

    sceKernelChangeThreadPriority(0, 0x10000100);
    test_exit();
    return 0;
}
