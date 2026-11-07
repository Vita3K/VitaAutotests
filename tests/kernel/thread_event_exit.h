// Shared by the thread_event_handler_exit_* tests. The main thread registers a handler for a target
// thread T with the START or END mask, prints the result and a line that it starts T, then starts T
// and waits for T's end with a long timeout. The handler calls sceKernelExitThread or
// sceKernelExitDeleteThread from inside its run. Only once the wait returned does main print its
// result, T's exit status, whether T's entry ran, and a last line that shows it went on. So an exit
// that ends the app leaves the output at the line before the start.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static volatile int entry_ran;
static int use_delete;

static int exit_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    if (use_delete)
        sceKernelExitDeleteThread(0x55);
    else
        sceKernelExitThread(0x55);
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    entry_ran = 1;
    return 0x66;
}

static void exit_run(SceUInt32 mask, int delete) {
    use_delete = delete;
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("register the handler", test_created(sceKernelRegisterThreadEventHandler("exit", target, mask, exit_handler, NULL)));
    PRINT("start the target", 1);
    sceKernelStartThread(target, 0, NULL);
    int status = 0xDEAD;
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINT("the wait for the target's end", sceKernelWaitThreadEnd(target, &status, &timeout));
    PRINT("its exit status", status);
    PRINT("its entry ran", entry_ran);
    PRINT("the main thread goes on", 1);
}
