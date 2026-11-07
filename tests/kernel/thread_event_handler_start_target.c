// What sceKernelStartThread returns for a thread that runs its own thread event handler. A handler
// for a target T blocks on a semaphore in the run of one event. While T waits there, the main thread
// starts T and prints the result, then signals the semaphore and waits for T's end. The cases:
// - T blocks in its START handler, before its entry ran
// - T blocks in its END handler, after its entry returned
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID sema;
static SceUInt32 blocking_event;
static volatile int blocked;

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)thread_id;
    (void)arg;
    (void)common;
    if ((SceUInt32)type == blocking_event) {
        blocked = 1;
        SceUInt timeout = 10 * TEST_WAIT_US;
        sceKernelWaitSema(sema, 1, &timeout);
    }
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static void run(const char *name, SceUInt32 event) {
    blocking_event = event;
    blocked = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("block", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(blocked && test_in_wait(target, WAIT_TYPE_SEMA, sema));
    PRINTF(sceKernelStartThread(target, 0, NULL), "%s: start the target", name);
    sceKernelSignalSema(sema, 1);
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: the wait for its end", name);
    sceKernelUnregisterThreadEventHandler(id);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);
}

int main(void) {
    run("in its START handler", SCE_KERNEL_THREAD_EVENT_TYPE_START);
    run("in its END handler", SCE_KERNEL_THREAD_EVENT_TYPE_EXIT);

    test_exit();
    return 0;
}
