// Which error sceKernelRegisterThreadEventHandler gives when the handler address is not executable
// and another argument is wrong as well. The handler address check comes last, so the earlier
// error should win. Each case registers a handler and prints the error code, or 0 when it was
// taken (and then unregisters it). The cases use a data address as the handler and then a code
// address as a control. Thread 0 with the mask 4 (it must be exactly 8), another thread with the
// mask 1, another thread with the mask 0x10 (it must be a subset of 0xC), and an unknown thread
// UID. Then valid arguments, thread 0 with the mask 8 and another thread with the masks 4 and 0,
// with a data address (the address error) and a code address (taken).
//
// On a PS Vita TV on firmware 3.65 thread 0 with the mask 4, the target thread with the mask 1 and
// the target thread with the mask 0x10 all return 0x80020005 (INVALID_ARGUMENT), with the data
// address and with the code address alike. The unknown thread UID returns 0x80028021
// (UNKNOWN_THREAD_ID) for the mask 4 with both kinds of address and for the mask 1 with the data
// address. With valid arguments (thread 0 with the mask 8, the target thread with the masks 4 and
// 0) the data address returns 0x80020006 (ILLEGAL_ADDR) and the code address is taken (0). So the
// address check comes after the thread and mask checks.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x7FFFFFF1

static volatile int data_word;
static SceUID target;

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int code_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    return 0;
}

static void try_register(const char *label, SceUID thread, SceUInt32 mask, void *address) {
    const SceUID id = sceKernelRegisterThreadEventHandler("h", thread, mask, (SceKernelThreadEventHandler)address, NULL);
    PRINT(label, id < 0 ? id : 0);
    if (id >= 0)
        sceKernelUnregisterThreadEventHandler(id);
}

int main(void) {
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("create the target thread", test_created(target));
    void *const data = (void *)&data_word;
    void *const code = (void *)code_handler;

    try_register("thread 0, mask 4, data handler", 0, 4, data);
    try_register("thread 0, mask 4, code handler", 0, 4, code);
    try_register("target, mask 1, data handler", target, 1, data);
    try_register("target, mask 1, code handler", target, 1, code);
    try_register("target, mask 0x10, data handler", target, 0x10, data);
    try_register("target, mask 0x10, code handler", target, 0x10, code);
    try_register("unknown thread, mask 4, data handler", UNKNOWN_UID, 4, data);
    try_register("unknown thread, mask 4, code handler", UNKNOWN_UID, 4, code);
    try_register("unknown thread, mask 1, data handler", UNKNOWN_UID, 1, data);

    try_register("thread 0, mask 8, data handler", 0, 8, data);
    try_register("thread 0, mask 8, code handler", 0, 8, code);
    try_register("target, mask 4, data handler", target, 4, data);
    try_register("target, mask 4, code handler", target, 4, code);
    try_register("target, mask 0, data handler", target, 0, data);
    try_register("target, mask 0, code handler", target, 0, code);

    sceKernelDeleteThread(target);
    test_exit();
    return 0;
}
