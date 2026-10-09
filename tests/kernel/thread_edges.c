// Thread edge cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int status_value;

static int return_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return status_value;
}

static int exit_delete_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitDeleteThread(5);
    return 0;
}

static int priority_result, priority_after;

static int priority_thread(SceSize args, void *argp) {
    const int priority = *(const int *)argp;
    (void)args;
    priority_result = sceKernelChangeThreadPriority(sceKernelGetThreadId(), priority);
    priority_after = sceKernelGetThreadCurrentPriority();
    return 0;
}

int main(void) {
    int status;
    SceUInt timeout;

    PRINT("wait for an unknown thread", sceKernelWaitThreadEnd(0x12345, &status, NULL));
    PRINT("delay of 0", sceKernelDelayThread(0));
    PRINT("delay with callbacks of 0", sceKernelDelayThreadCB(0));
    PRINT("the main thread's priority", sceKernelGetThreadCurrentPriority());

    // A negative return value as the exit status
    status_value = -2;
    SceUID thread = test_start_thread(return_thread);
    status = 0;
    sceKernelWaitThreadEnd(thread, &status, NULL);
    sceKernelDeleteThread(thread);
    PRINT("a negative exit status", status);

    // A thread that deletes itself on exit
    thread = test_start_thread(exit_delete_thread);
    test_wait_until_waiting(thread);
    status = 0xDEAD;
    timeout = 1000;
    PRINT("wait for a thread that exited and deleted itself", sceKernelWaitThreadEnd(thread, &status, &timeout));
    PRINT("its status", status);
    PRINT("its exit status", sceKernelGetThreadExitStatus(thread, &status));
    PRINT("delete it again", sceKernelDeleteThread(thread));

    // Priorities set through sceKernelChangeThreadPriority
    static const int priorities[] = { 0, 0x10000100, 0x100000E0, 0x40, 0xBF, 0x3F, 0xC0, 0x20 };
    for (unsigned int i = 0; i < sizeof(priorities) / sizeof(priorities[0]); i++) {
        test_join_thread(test_start_thread_arg(priority_thread, priorities[i]));
        char label[64];
        sceClibSnprintf(label, sizeof(label), "change the priority to 0x%X", priorities[i]);
        PRINT(label, priority_result);
        sceClibSnprintf(label, sizeof(label), "the priority after changing it to 0x%X", priorities[i]);
        PRINT(label, priority_after);
    }

    test_exit();
    return 0;
}
