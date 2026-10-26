// The priority a thread runs at in each run, and the initial priority in its info, when the thread
// was created with priority 0, which takes the creator's current priority. The main thread, at
// 0xA0, creates the thread, then sets itself to 0x50 before the thread's first run. The thread
// ends, is started again, and ends. After each run the test prints whether the priority the thread
// ran at and the info's initial priority equal 0xA0, the priority the thread was created with.
// Models thread_create_current_priority, which creates from threads at other priorities.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define CREATE_PRIORITY 0xA0

static volatile int seen_priority[2], runs;

static int thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    seen_priority[runs++] = sceKernelGetThreadCurrentPriority();
    return 0;
}

int main(void) {
    sceKernelChangeThreadPriority(0, CREATE_PRIORITY);
    const SceUID thread = sceKernelCreateThread("zero", thread_entry, 0, 0x4000, 0, 0, NULL);
    sceKernelChangeThreadPriority(0, 0x50);

    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINT("run 1: it runs at the creator's priority at creation", seen_priority[0] == CREATE_PRIORITY);
    PRINT("run 1: its initial priority in the info is the creator's priority at creation", info.initPriority == CREATE_PRIORITY);

    sceKernelStartThread(thread, 0, NULL);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelGetThreadInfo(thread, &info);
    PRINT("run 2: it runs at the creator's priority at creation", seen_priority[1] == CREATE_PRIORITY);
    PRINT("run 2: its initial priority in the info is the creator's priority at creation", info.initPriority == CREATE_PRIORITY);

    PRINT("delete it", sceKernelDeleteThread(thread));
    sceKernelChangeThreadPriority(0, SCE_KERNEL_DEFAULT_PRIORITY_USER);
    test_exit();
    return 0;
}
