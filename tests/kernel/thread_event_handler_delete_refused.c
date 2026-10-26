// A thread event handler that tries to delete another thread. Main creates a thread D that is
// never started, registers a START and END handler for a target, starts the target and waits for
// its end. The START run calls sceKernelDeleteThread on D and records the result. The handler
// counts its runs in both events. Prints the handler's delete result, whether D still exists after
// the target ended, the handler's run count, and main's own delete of D. The handler deletes only
// D, never the thread it runs on.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID target, dormant, own_handler;
static volatile int handler_runs, delete_result;

// Counts its runs. On the START run it deletes the dormant thread and records the result.
static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)thread_id;
    (void)arg;
    (void)common;
    handler_runs++;
    if (type == SCE_KERNEL_THREAD_EVENT_TYPE_START)
        delete_result = sceKernelDeleteThread(dormant);
    return 0;
}

// Does nothing, for the target and the dormant thread
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    dormant = sceKernelCreateThread("dormant", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    own_handler = sceKernelRegisterThreadEventHandler("handler", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINT("register the handler", test_created(own_handler));

    delete_result = 0xDEAD;
    sceKernelStartThread(target, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "the wait for the end of the target");

    PRINT("the handler's delete of the dormant thread", delete_result);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINT("the dormant thread still exists", sceKernelGetThreadInfo(dormant, &info) == 0);
    PRINT("the handler runs", handler_runs);
    PRINT("main's delete of the dormant thread", sceKernelDeleteThread(dormant));

    sceKernelUnregisterThreadEventHandler(own_handler);
    sceKernelDeleteThread(target);
    test_exit();
    return 0;
}
