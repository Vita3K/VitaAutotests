// Another thread X ends the process while a thread T runs its END thread event handler, which spins
// without any call. The main thread registers the handler with the END mask alone. T runs on core
// 0, the main thread and X on core 1. X calls sceKernelExitProcess without printing DONE. If the
// exit ends T and the app, the output stops after the handler line. The main thread polls for 5 s,
// far longer than a working exit takes, and then prints a line and DONE, so an exit that can't end
// T in that time shows. The spin is bounded, so even then the app ends, only late.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

// Bounds the spin, so a run where the exit can't end T still ends
#define SPINS 3000000000u

static volatile int running;
static volatile unsigned int counter;

static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    running = 1;
    while (counter < SPINS)
        counter++;
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0x55;
}

// Ends the process without printing DONE, so a run where the exit can't end the app shows
static int exiting_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitProcess(0);
    return 0;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID handler = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINT("register the END handler", test_created(handler));
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(running);
    PRINT("the END handler runs", running);

    const SceUID exiting = sceKernelCreateThread("exiting", exiting_thread, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(exiting, 0, NULL);
    for (int i = 0; i < 500; i++)
        sceKernelDelayThread(10000);
    PRINT("the process is still there", 1);
    test_exit();
    return 0;
}
