// A thread event handler that is unregistered after its thread has ended and been deleted. The
// test registers a handler for the start and exit events of a thread that returns 7, starts the
// thread, and waits for its end. It prints the registration, the exit status, the number of
// handler runs before the delete, the delete's result, whether the delete changed the number of
// handler runs, and the result of unregistering the handler after the delete.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static volatile int handler_runs;

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    handler_runs++;
    return 0;
}

static int returns_7(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 7;
}

int main(void) {
    const SceUID thread = sceKernelCreateThread("target", returns_7, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("handler", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINTF(test_created(id), "register");
    sceKernelStartThread(thread, 0, NULL);
    int status = 0xDEAD;
    sceKernelWaitThreadEnd(thread, &status, NULL);
    PRINT("the exit status", status);
    const int runs_before_delete = handler_runs;
    PRINT("handler runs before the delete", runs_before_delete);

    PRINT("delete the ended thread", sceKernelDeleteThread(thread));
    PRINT("the delete changed the handler runs", handler_runs != runs_before_delete);
    if (id >= 0)
        PRINT("unregister the handler", sceKernelUnregisterThreadEventHandler(id));

    test_exit();
    return 0;
}
