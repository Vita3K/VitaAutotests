// libkernel's own sceKernelGetThreadTLSAddr, NID 35D20E49 in SceLibKernel, which vitasdk has no
// stub for. The eight checks: thread 0 and the caller's UID give the caller's slot, key 1 of thread
// 0 gives the next word, the thread manager's sceKernelGetThreadTLSAddr gives another thread its own
// slot, this call gives that same slot, and an unknown UID, key 256 and key -1 give NULL.
//
// On a PS Vita TV on firmware 3.65 all eight checks hold.
#include <psp2/kernel/threadmgr.h>

#include "extra_imports.h"
#include "test.h"

#define UNKNOWN_UID 0x7FFFFFF1

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    void *const own = sceKernelGetTLSAddr(0);
    PRINT("thread 0 gives the caller's slot", SceLibKernel_35D20E49(0, 0) == own);
    PRINT("the caller's UID gives the caller's slot", SceLibKernel_35D20E49(sceKernelGetThreadId(), 0) == own);
    PRINT("key 1 of thread 0 is the next word", SceLibKernel_35D20E49(0, 1) == sceKernelGetTLSAddr(1));

    // A dormant thread already has its slots
    const SceUID other = sceKernelCreateThread("other", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    void *const other_slot = sceKernelGetThreadTLSAddr(other, 0);
    PRINT("the thread manager call gives another thread a slot", other_slot != NULL && other_slot != own);
    PRINT("another thread gives that thread's slot", SceLibKernel_35D20E49(other, 0) == other_slot);
    sceKernelDeleteThread(other);

    PRINT("an unknown UID gives NULL", SceLibKernel_35D20E49(UNKNOWN_UID, 0) == NULL);
    PRINT("key 256 gives NULL", SceLibKernel_35D20E49(0, 256) == NULL);
    PRINT("key -1 gives NULL", SceLibKernel_35D20E49(0, -1) == NULL);

    test_exit();
    return 0;
}
