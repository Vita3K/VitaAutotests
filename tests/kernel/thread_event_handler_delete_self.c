// What sceKernelDeleteThread returns inside a thread event handler for the thread the handler runs
// on. A handler for a target T with the START and END mask calls sceKernelDeleteThread with T's UID
// and with 0 in each run, and records the results and its run count. The main thread prints them
// once T has ended, then T's exit status and whether T's entry ran.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static volatile int runs, entry_ran;
static int by_uid[2], by_zero[2];

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    (void)common;
    const int i = type == SCE_KERNEL_THREAD_EVENT_TYPE_START ? 0 : 1;
    by_uid[i] = sceKernelDeleteThread(thread_id);
    by_zero[i] = sceKernelDeleteThread(0);
    runs++;
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    entry_ran = 1;
    return 0x66;
}

int main(void) {
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("self", target, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, NULL);
    PRINT("register the handler", test_created(id));
    sceKernelStartThread(target, 0, NULL);
    int status = 0xDEAD;
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINT("the wait for the target's end", sceKernelWaitThreadEnd(target, &status, &timeout));
    PRINT("its exit status", status);
    PRINT("its entry ran", entry_ran);
    PRINT("the handler's runs", runs);
    PRINT("START: delete the target by its UID", by_uid[0]);
    PRINT("START: delete thread 0", by_zero[0]);
    PRINT("END: delete the target by its UID", by_uid[1]);
    PRINT("END: delete thread 0", by_zero[1]);
    PRINT("unregister the handler", sceKernelUnregisterThreadEventHandler(id));
    PRINT("delete the target", sceKernelDeleteThread(target));

    test_exit();
    return 0;
}
