// Thread event handler registrations with START and END, for a dormant thread and for every thread,
// first with the name "handler" and then with a NULL name. The named registrations are the control.
// A refused registration prints its error as it comes, and a created one prints 1 and is
// unregistered at once, with the unregister result printed after it. The NULL name cases go last,
// since an implementation that reads the name could crash on them.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static int idle_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int plain_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    return 0;
}

// Registers a handler with name for thread, then unregisters it if that worked
static void register_named(const char *label, const char *name, SceUID thread) {
    const SceUID id = sceKernelRegisterThreadEventHandler(name, thread, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, plain_handler, NULL);
    PRINTF(id < 0 ? id : 1, "%s: register", label);
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "%s: unregister", label);
}

int main(void) {
    const SceUID dormant = sceKernelCreateThread("dormant", idle_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);

    register_named("named, dormant thread", "handler", dormant);
    register_named("named, every thread", "handler", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID);
    register_named("NULL name, dormant thread", NULL, dormant);
    register_named("NULL name, every thread", NULL, SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID);

    sceKernelDeleteThread(dormant);
    test_exit();
    return 0;
}
