// Which error thread manager calls give inside a thread event handler when an argument is wrong as
// well. A START handler for a target T makes each call below once and records the results, and the
// main thread prints them once T has ended. The calls: start thread 0, start an unknown UID, create
// a thread with a NULL name, create one with an affinity mask outside the user cores, register a
// handler with a NULL name, register one for an unknown thread, unregister a semaphore's UID,
// unregister an unknown UID, delete thread 0 and delete an unknown UID. Then, from the main thread,
// registrations with a handler at a data address, at NULL, and at a code address.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x7FFFFFF1
#define CALLS 10

static const char *const names[CALLS] = {
    "start thread 0",
    "start an unknown UID",
    "create a thread with a NULL name",
    "create a thread with the affinity mask 0x80000",
    "register a handler with a NULL name",
    "register a handler for an unknown thread",
    "unregister a semaphore's UID",
    "unregister an unknown UID",
    "delete thread 0",
    "delete an unknown UID",
};

static SceUID sema;
static int results[CALLS], runs;
static volatile int data_word;

static int dummy_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int dummy_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    return 0;
}

static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    results[0] = sceKernelStartThread(0, 0, NULL);
    results[1] = sceKernelStartThread(UNKNOWN_UID, 0, NULL);
    results[2] = sceKernelCreateThread(NULL, dummy_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    results[3] = sceKernelCreateThread("t", dummy_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0x80000, NULL);
    results[4] = sceKernelRegisterThreadEventHandler(NULL, SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START, dummy_handler, NULL);
    results[5] = sceKernelRegisterThreadEventHandler("h", UNKNOWN_UID, SCE_KERNEL_THREAD_EVENT_TYPE_START, dummy_handler, NULL);
    results[6] = sceKernelUnregisterThreadEventHandler(sema);
    results[7] = sceKernelUnregisterThreadEventHandler(UNKNOWN_UID);
    results[8] = sceKernelDeleteThread(0);
    results[9] = sceKernelDeleteThread(UNKNOWN_UID);
    runs++;
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static void register_at(const char *label, void *address) {
    const SceUID id = sceKernelRegisterThreadEventHandler("h", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START, (SceKernelThreadEventHandler)address, NULL);
    PRINT(label, test_created(id));
    if (id >= 0)
        sceKernelUnregisterThreadEventHandler(id);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("probe", target, SCE_KERNEL_THREAD_EVENT_TYPE_START, handler, NULL);
    PRINT("register the handler", test_created(id));
    sceKernelStartThread(target, 0, NULL);
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINT("the wait for the target's end", sceKernelWaitThreadEnd(target, NULL, &timeout));
    PRINT("the handler's runs", runs);
    for (int i = 0; i < CALLS; i++)
        PRINT(names[i], results[i] < 0 ? results[i] : 0);
    sceKernelUnregisterThreadEventHandler(id);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);

    register_at("register a handler at a data address", (void *)&data_word);
    register_at("register a handler at NULL", NULL);
    register_at("register a handler at a code address", (void *)dummy_handler);

    test_exit();
    return 0;
}
