// What a thread waiting for a thread signal reports as the UID it waits on: 0, itself, -1, the
// process, the thread that sends the signal, or something else, and its class. Also whether two
// waiting threads report the same one, and the same for a wait with callbacks. The value itself
// isn't printed, since it differs between consoles.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

static volatile int with_callbacks;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return with_callbacks ? sceKernelWaitSignalCB(0, 0, NULL) : sceKernelWaitSignal(0, 0, NULL);
}

static int wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.waitType : -1;
}

static SceUID wait_id(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitId : -2;
}

static void run(const char *name) {
    const SceUID a = test_start_thread(waiter);
    const SceUID b = test_start_thread(waiter);
    TEST_WAIT_FOR(wait_type(a) != 0 && wait_type(b) != 0);
    const SceUID id = wait_id(a);
    PRINTF(wait_type(a), "%s: the wait type", name);
    PRINTF(id == 0, "%s: the wait UID is 0", name);
    PRINTF(id == a, "%s: the wait UID is the waiting thread", name);
    PRINTF(id == -1, "%s: the wait UID is -1", name);
    PRINTF(id == sceKernelGetProcessId(), "%s: the wait UID is the process", name);
    PRINTF(id == sceKernelGetThreadId(), "%s: the wait UID is the thread that will send the signal", name);
    PRINTF(get_uid_class(id), "%s: the class of the wait UID", name);
    PRINTF(wait_id(b) == id, "%s: a second waiting thread reports the same UID", name);
    sceKernelSendSignal(a);
    sceKernelSendSignal(b);
    test_join_thread(a);
    test_join_thread(b);
}

int main(void) {
    run("a plain wait");
    with_callbacks = 1;
    run("a wait with callbacks");
    test_exit();
    return 0;
}
