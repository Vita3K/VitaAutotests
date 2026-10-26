// A thread event handler that its target unregisters from the target's entry, after the START run.
// The handler counts its START and END runs. Cases:
// - control: the entry leaves the handler registered. Prints the wait for the end, the runs of
//   START and END, and main's unregister result after the target ended.
// - unregister in the entry: the entry calls sceKernelUnregisterThreadEventHandler, then returns.
//   Prints the entry's unregister result as well, and the same lines as the control.
// The entry runs on the target after its START run, so the handler's registration state is known
// when the target ends. Each case has its own target and handler.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID target, own_handler;
static volatile int start_runs, end_runs, entry_unregister;

// Counts the START and END runs
static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)thread_id;
    (void)arg;
    (void)common;
    if (type == SCE_KERNEL_THREAD_EVENT_TYPE_START)
        start_runs++;
    else
        end_runs++;
    return 0;
}

// Unregisters the handler from its own entry if the argument is 1
static int target_entry(SceSize args, void *argp) {
    (void)args;
    if (*(const int *)argp)
        entry_unregister = sceKernelUnregisterThreadEventHandler(own_handler);
    return 0;
}

// Runs a target with the handler, where its entry unregisters the handler if unregister is 1
static void run(const char *name, int unregister) {
    start_runs = end_runs = 0;
    entry_unregister = 0xDEAD;
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    own_handler = sceKernelRegisterThreadEventHandler("handler", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINTF(test_created(own_handler), "%s: register", name);

    sceKernelStartThread(target, sizeof(unregister), &unregister);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: the wait for the end", name);
    if (unregister)
        PRINTF(entry_unregister, "%s: the entry's unregister", name);
    PRINTF(start_runs, "%s: START runs", name);
    PRINTF(end_runs, "%s: END runs", name);
    PRINTF(sceKernelUnregisterThreadEventHandler(own_handler), "%s: unregister from the main thread", name);
    sceKernelDeleteThread(target);
}

int main(void) {
    run("control", 0);
    run("unregister in the entry", 1);

    test_exit();
    return 0;
}
