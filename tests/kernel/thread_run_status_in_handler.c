// sceKernelGetThreadRunStatus called from a thread event handler. A START and END handler is
// registered for a target, and the handler reads the run status into a struct on its own stack
// on each of its runs. It counts the entries that hold the target, and whether the process of
// the entry is the process of the caller. The runs are logged and printed after the target
// ended, with the call's result, for the start run and the end run.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_RUNS 4

static SceUID target;
static volatile int log_count;
static volatile int log_type[MAX_RUNS], log_result[MAX_RUNS], log_holders[MAX_RUNS], log_process[MAX_RUNS];

// Reads the run status and logs the call's result, the entries holding the target, and whether the
// process of the entry that holds it is the caller's
static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    if (log_count >= MAX_RUNS)
        return 0;
    SceKernelThreadRunStatus status = { .size = sizeof(status) };
    const int result = get_thread_run_status(&status);
    const SceUID process = sceKernelGetProcessId();
    int holders = 0;
    int process_matches = 0;
    for (int i = 0; i < 4; i++) {
        if (status.cpuInfo[i].threadId != thread_id)
            continue;
        holders++;
        process_matches = status.cpuInfo[i].processId == process;
    }
    const int i = log_count++;
    log_type[i] = type;
    log_result[i] = result;
    log_holders[i] = holders;
    log_process[i] = process_matches;
    return 0;
}

// Does nothing, the handler is what the case looks at
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("handler", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINT("register the handler", test_created(id));

    sceKernelStartThread(target, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "the wait for the end of the target");
    PRINT("handler runs", log_count);
    for (int i = 0; i < log_count; i++) {
        const char *when = log_type[i] == SCE_KERNEL_THREAD_EVENT_TYPE_START ? "start" : "end";
        PRINTF(log_result[i], "%s: the call", when);
        PRINTF(log_holders[i], "%s: entries holding the target", when);
        PRINTF(log_process[i], "%s: the entry's process is the caller's", when);
    }

    sceKernelUnregisterThreadEventHandler(id);
    sceKernelDeleteThread(target);
    test_exit();
    return 0;
}
