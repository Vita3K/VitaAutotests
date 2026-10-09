#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID sema;
static int runs;
static int arg_size, arg_value;
static int self_delete;
static int priority_change, priority_after;

static int return_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    runs++;
    return 42;
}

static int exit_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitThread(7);
    return 0;
}

static int args_thread(SceSize args, void *argp) {
    arg_size = args;
    arg_value = *(const int *)argp;
    return 0;
}

// Waits on the semaphore, with a timeout so the test ends even if a check fails
static int blocked_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 500000;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

static int delete_self_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    self_delete = sceKernelDeleteThread(sceKernelGetThreadId());
    return 0;
}

static int priority_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    priority_change = sceKernelChangeThreadPriority(sceKernelGetThreadId(), 100);
    priority_after = sceKernelGetThreadCurrentPriority();
    return 0;
}

int main(void) {
    int status = -1;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);

    SceUID thread = sceKernelCreateThread("return", return_thread, 0x10000100, 0x4000, 0, 0, NULL);
    PRINT("exit status of a thread never started", sceKernelGetThreadExitStatus(thread, &status));
    sceKernelStartThread(thread, 0, NULL);
    PRINT("wait for the end", sceKernelWaitThreadEnd(thread, &status, NULL));
    PRINT("the wait gives the returned status", status);
    status = -1;
    PRINT("exit status of an ended thread", sceKernelGetThreadExitStatus(thread, &status));
    PRINT("the exit status is the returned one", status);
    PRINT("start an ended thread again", sceKernelStartThread(thread, 0, NULL));
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("the thread ran twice", runs);
    PRINT("delete an ended thread", sceKernelDeleteThread(thread));
    PRINT("exit status of a deleted thread", sceKernelGetThreadExitStatus(thread, &status));

    thread = test_start_thread(exit_thread);
    sceKernelWaitThreadEnd(thread, &status, NULL);
    sceKernelDeleteThread(thread);
    PRINT("the status given to sceKernelExitThread", status);

    test_join_thread(test_start_thread_arg(args_thread, 1234));
    PRINT("the argument size", arg_size);
    PRINT("the argument block", arg_value);

    // A running thread can't be started, deleted or asked for its exit status
    thread = test_start_thread(blocked_thread);
    test_wait_until_waiting(thread);
    PRINT("start a running thread", sceKernelStartThread(thread, 0, NULL));
    PRINT("delete a running thread", sceKernelDeleteThread(thread));
    PRINT("exit status of a running thread", sceKernelGetThreadExitStatus(thread, &status));
    SceUInt timeout = 1000;
    PRINT("wait for the end times out", sceKernelWaitThreadEnd(thread, &status, &timeout));
    sceKernelSignalSema(sema, 1);
    test_join_thread(thread);

    test_join_thread(test_start_thread(delete_self_thread));
    PRINT("a thread deletes itself", self_delete);

    test_join_thread(test_start_thread(priority_thread));
    PRINT("change the own priority", priority_change);
    PRINT("the current priority is the new one", priority_after);

    test_exit();
    return 0;
}
