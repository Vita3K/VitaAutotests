// A thread T waits in a delay with callbacks and runs a notified callback, which waits in a delay
// without callbacks for 8 s. The main thread waits until T is in that inner delay and prints
// whether it got there. Then another thread X calls sceKernelExitProcess without printing DONE. If
// the exit ends T and the app, the output stops after that line. The main thread polls for 4 s,
// half the inner delay, and then prints a line and DONE, so an exit that waits for the inner delay
// to end shows.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID callback, target;
static volatile int callback_made, entered;

static int wait_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    entered = 1;
    sceKernelDelayThread(4 * TEST_WAIT_US);
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("wait", 0, wait_callback, NULL);
    callback_made = 1;
    sceKernelDelayThreadCB(20 * TEST_WAIT_US);
    return 0;
}

// Ends the process without printing DONE, so a run where the exit can't end the app shows
static int exiting_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitProcess(0);
    return 0;
}

// Returns whether T waits in the delay without callbacks
static int in_inner_delay(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return entered && sceKernelGetThreadInfo(target, &info) == 0 && info.status == SCE_THREAD_WAITING && info.waitType == WAIT_TYPE_DELAY;
}

int main(void) {
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(callback_made);
    test_wait_until_waiting(target);
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(in_inner_delay());
    PRINT("the callback waits in a delay", in_inner_delay());

    test_start_thread(exiting_thread);
    for (int i = 0; i < 400; i++)
        sceKernelDelayThread(10000);
    PRINT("the process is still there", 1);
    test_exit();
    return 0;
}
