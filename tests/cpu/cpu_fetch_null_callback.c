// A call to address 0 in ARM state from a callback, with the harness of fetch_fault.h. The worker
// notifies its own callback and runs it with sceKernelCheckCallback, and the callback makes the
// call. An exit handler registered on the worker prints a line if it runs.
//
// On a PS Vita TV on firmware 3.65 the process ends with a core dump when the callback makes the
// call, so the exit handler doesn't run and the output stops at the release line.
#include "fetch_fault.h"

static int fault_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    fetch_call();
    return 0;
}

static int callback_worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const SceUID callback = sceKernelCreateCallback("fault", 0, fault_callback, NULL);
    sceKernelNotifyCallback(callback, 1);
    sceKernelCheckCallback();
    sceKernelSignalSema(fetch_done, 1);
    return 7;
}

static int exit_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    PRINT("the worker's exit handler ran", 1);
    return 0;
}

int main(void) {
    const SceUID worker = fetch_create_worker(callback_worker);
    PRINT("create the worker", worker > 0);
    PRINT("register its exit handler", sceKernelRegisterThreadEventHandler("exit", worker, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, exit_handler, NULL) > 0);
    fetch_run(worker, 0);
    test_exit();
    return 0;
}
