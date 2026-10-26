// Thread event handlers for every thread (0x10027), registered while a target thread already
// waits on a semaphore, and registered before the target starts. Case a starts the target, waits
// until it waits on the semaphore, registers a START and END handler for every thread, then
// signals and joins the target. Case b registers the same handler before the start. Each case
// prints the START and END runs for the target, whether the thread ID argument of the last run is
// the target, and how many runs happened on the target itself. Other threads of the test may reach
// the handler, so only the runs for the target are counted.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID target, go;
static volatile int start_runs, end_runs, on_target_runs, runs, last_arg;

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    runs++;
    last_arg = thread_id;
    if (thread_id != target)
        return 0;
    if (type == SCE_KERNEL_THREAD_EVENT_TYPE_START)
        start_runs++;
    else
        end_runs++;
    if (sceKernelGetThreadId() == target)
        on_target_runs++;
    return 0;
}

static SceUID register_every_thread(void) {
    return sceKernelRegisterThreadEventHandler("every", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
}

// Runs one case. before_start registers the handler before the start, else while the target waits.
static void run_case(const char *name, int before_start) {
    start_runs = end_runs = on_target_runs = runs = last_arg = 0;
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    SceUID id = -1;
    if (before_start)
        id = register_every_thread();
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, go));
    if (!before_start)
        id = register_every_thread();
    sceKernelSignalSema(go, 1);

    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: the wait for the end", name);
    PRINTF(id < 0 ? id : 1, "%s: register", name);
    PRINTF(start_runs, "%s: START runs for the target", name);
    PRINTF(end_runs, "%s: END runs for the target", name);
    PRINTF(runs > 0 && last_arg == target, "%s: the thread ID argument of the last run is the target", name);
    PRINTF(on_target_runs, "%s: runs on the target", name);
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "%s: unregister", name);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(go);
}

int main(void) {
    run_case("a: registered while waiting", 0);
    run_case("b: registered before the start", 1);

    test_exit();
    return 0;
}
