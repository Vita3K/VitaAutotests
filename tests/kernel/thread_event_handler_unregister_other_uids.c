// What an unregister of a UID that isn't a thread event handler gives, inside an END handler and
// from the main thread. Thread T's END handler unregisters T's own UID, a memory block's UID, a
// semaphore's UID and a UID nothing has, in that order, then returns 0. Once T has ended, the main
// thread unregisters the same four UIDs.
//
// On a PS Vita TV on firmware 3.65 every UID the process has gives 0x80027101 inside the END
// handler, and only the UID nothing has gives 0x80028061 there. From the main thread all four give
// 0x80028061.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x7FFFFFF1

static SceUID thread, block, sema;
static int inside[4] = { 0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD };

static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    inside[0] = sceKernelUnregisterThreadEventHandler(thread);
    inside[1] = sceKernelUnregisterThreadEventHandler(block);
    inside[2] = sceKernelUnregisterThreadEventHandler(sema);
    inside[3] = sceKernelUnregisterThreadEventHandler(UNKNOWN_UID);
    return 0;
}

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    block = sceKernelAllocMemBlock("block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    thread = sceKernelCreateThread("T", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID handler = sceKernelRegisterThreadEventHandler("end", thread, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINT("register the handler", test_created(handler));
    sceKernelStartThread(thread, 0, NULL);
    SceUInt timeout = 10 * TEST_WAIT_US;
    PRINT("the wait for T's end", sceKernelWaitThreadEnd(thread, NULL, &timeout));

    PRINT("in the END handler: unregister T's UID", inside[0]);
    PRINT("in the END handler: unregister a memory block's UID", inside[1]);
    PRINT("in the END handler: unregister a semaphore's UID", inside[2]);
    PRINT("in the END handler: unregister a UID nothing has", inside[3]);
    PRINT("from the main thread: unregister T's UID", sceKernelUnregisterThreadEventHandler(thread));
    PRINT("from the main thread: unregister a memory block's UID", sceKernelUnregisterThreadEventHandler(block));
    PRINT("from the main thread: unregister a semaphore's UID", sceKernelUnregisterThreadEventHandler(sema));
    PRINT("from the main thread: unregister a UID nothing has", sceKernelUnregisterThreadEventHandler(UNKNOWN_UID));

    sceKernelUnregisterThreadEventHandler(handler);
    sceKernelDeleteThread(thread);
    sceKernelDeleteSema(sema);
    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
