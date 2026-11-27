// Whether the sysmodule calls stay locked while a module that sceSysmoduleLoadModule loads runs its
// start function. A START thread event handler runs on the thread of libfiber's start function
// while the load of libfiber is in progress. For the first thread that starts during the load, it
// wakes a helper thread H through a semaphore and waits on a second semaphore, with a timeout. H
// records whether the handler was still waiting, calls sceSysmoduleIsLoaded for libfiber, records
// whether the handler had already ended, and signals the second semaphore. The test prints the
// load's result, whether the handler ran, the result of the handler's wait, whether H made its call
// while the handler waited, H's result and whether H returned only after the handler ended. Then it
// unloads libfiber.
//
// On a PS Vita TV on firmware 3.65 the handler's wait times out with 0x80028005, and H's
// sceSysmoduleIsLoaded returns only after the handler ended, with 0 once the load finished.
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include "test.h"

// How long the handler waits for H, far past the time H needs when nothing blocks it
#define HANDLER_WAIT_US TEST_WAIT_US

static SceUID wake_h, h_done;
static volatile int armed, handler_ran, handler_wait, handler_ended;
static volatile int h_called, h_result, h_after_handler, h_cancel;

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    if (!armed)
        return 0;
    armed = 0;
    handler_ran = 1;
    sceKernelSignalSema(wake_h, 1);
    SceUInt32 timeout = HANDLER_WAIT_US;
    handler_wait = sceKernelWaitSema(h_done, 1, &timeout);
    handler_ended = 1;
    return 0;
}

static int h_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelWaitSema(wake_h, 1, NULL);
    if (h_cancel)
        return 0;
    h_called = !handler_ended;
    h_result = sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER);
    h_after_handler = handler_ended;
    sceKernelSignalSema(h_done, 1);
    return 0;
}

int main(void) {
    wake_h = sceKernelCreateSema("wake_h", 0, 0, 1, NULL);
    h_done = sceKernelCreateSema("h_done", 0, 0, 1, NULL);
    // H starts before the handler is registered, so its own start is not seen
    const SceUID h = test_start_thread(h_entry);
    test_wait_until_waiting(h);

    const SceUID id = sceKernelRegisterThreadEventHandler("lock", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START, handler, NULL);
    armed = 1;
    const int loaded = sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER);
    armed = 0;
    // Without a start thread H is woken to end without its call
    if (!handler_ran) {
        h_cancel = 1;
        sceKernelSignalSema(wake_h, 1);
    }
    test_join_thread(h);
    if (id >= 0)
        sceKernelUnregisterThreadEventHandler(id);

    PRINT("register the handler", test_created(id));
    PRINT("load libfiber", loaded);
    PRINT("the handler ran for its start thread", handler_ran);
    if (handler_ran) {
        PRINT("the handler's wait for H", handler_wait);
        PRINT("H made its call while the handler waited", h_called);
        PRINT("H's sceSysmoduleIsLoaded", h_result);
        PRINT("H returned after the handler ended", h_after_handler);
    }
    PRINT("unload libfiber", sceSysmoduleUnloadModule(SCE_SYSMODULE_FIBER));

    sceKernelDeleteSema(wake_h);
    sceKernelDeleteSema(h_done);
    test_exit();
    return 0;
}
