// Thread event handler registrations with masks that hold bits other than START and END, and with
// START, END and both together, each for a fresh dormant thread that never starts. The masks are
// 0x1, 0x2, 0x10, 0x20, 0x100, 0x5 and 0x18, then 0x4, 0x8 and 0xC. A refused registration prints
// its error as it comes. A created one prints 1, then its unregister result, since it is removed
// at once before the thread is deleted.
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

// Registers a handler with mask for a fresh dormant thread, then unregisters it if that worked
static void register_mask(SceInt32 mask) {
    const SceUID thread = sceKernelCreateThread("dormant", idle_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("mask", thread, mask, plain_handler, NULL);
    PRINTF(id < 0 ? id : 1, "mask 0x%X: register", mask);
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "mask 0x%X: unregister", mask);
    sceKernelDeleteThread(thread);
}

int main(void) {
    static const SceInt32 masks[] = { 0x1, 0x2, 0x10, 0x20, 0x100, 0x5, 0x18, 0x4, 0x8, 0xC };
    for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); i++)
        register_mask(masks[i]);

    test_exit();
    return 0;
}
