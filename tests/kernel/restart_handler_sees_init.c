// What a start and exit handler of a thread sees when the thread is started again. The thread is
// created at priority 0x60 on core 1. Its first run raises its priority to 0x50 and moves to core 2
// before it ends, then it is started again. Each handler event logs the priority, affinity and
// status the thread shows, and how many runs of the entry came before it. Each run logs the
// priority and affinity its entry sees. The cases: the values at the start and the exit of both
// runs.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CREATE_PRIORITY 0x60
#define CREATE_AFFINITY SCE_KERNEL_CPU_MASK_USER_1
#define RAISED_PRIORITY 0x50
#define RAISED_AFFINITY SCE_KERNEL_CPU_MASK_USER_2

#define LOG_MAX 8

static SceUID target;
static volatile int runs, log_count;
static volatile int log_type[LOG_MAX], log_entry_runs[LOG_MAX], log_priority[LOG_MAX];
static volatile int log_affinity[LOG_MAX], log_status[LOG_MAX];
static volatile int run_priority[2], run_affinity[2];

// Logs what the target shows for a start or exit event
static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    if (thread_id != target || log_count >= LOG_MAX)
        return 0;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread_id, &info);
    const int i = log_count++;
    log_type[i] = type;
    log_entry_runs[i] = runs;
    log_priority[i] = info.currentPriority;
    log_affinity[i] = info.currentCpuAffinityMask;
    log_status[i] = info.status;
    return 0;
}

// Records the priority and affinity of its run, and in the first run raises its priority and moves
// to core 2 before it ends
static int target_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const int run = runs++;
    run_priority[run] = sceKernelGetThreadCurrentPriority();
    run_affinity[run] = sceKernelGetThreadCpuAffinityMask(0);
    if (run == 0) {
        sceKernelChangeThreadPriority(0, RAISED_PRIORITY);
        sceKernelChangeThreadCpuAffinityMask(0, RAISED_AFFINITY);
    }
    return 0;
}

int main(void) {
    target = sceKernelCreateThread("handlers", target_thread, CREATE_PRIORITY, 0x4000, 0, CREATE_AFFINITY, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("handler", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINT("register", test_created(id));

    sceKernelStartThread(target, 0, NULL);
    sceKernelWaitThreadEnd(target, NULL, NULL);
    sceKernelStartThread(target, 0, NULL);
    sceKernelWaitThreadEnd(target, NULL, NULL);

    PRINTF(log_count, "handler events");
    for (int i = 0; i < log_count; i++) {
        PRINTF(log_type[i], "event %d: the type", i + 1);
        PRINTF(log_entry_runs[i], "event %d: runs of the entry before it", i + 1);
        PRINTF(log_priority[i], "event %d: the priority", i + 1);
        PRINTF(log_affinity[i], "event %d: the affinity", i + 1);
        PRINTF(log_status[i], "event %d: the status", i + 1);
    }
    for (int r = 0; r < 2; r++) {
        PRINTF(run_priority[r], "run %d: the entry's priority", r + 1);
        PRINTF(run_affinity[r], "run %d: the entry's affinity", r + 1);
    }

    PRINT("unregister", sceKernelUnregisterThreadEventHandler(id));
    sceKernelDeleteThread(target);

    test_exit();
    return 0;
}
