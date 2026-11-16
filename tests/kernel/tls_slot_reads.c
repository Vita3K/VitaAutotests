// Whether sceKernelGetThreadId returns TLS slot 1 and sceKernelGetThreadCurrentPriority returns TLS
// slot 8. The test writes another value into the slot, makes the call at once and puts the old
// value back before anything else runs.
//
// On a PS Vita TV on firmware 3.65 sceKernelGetThreadId returns the written slot 1 and
// sceKernelGetThreadCurrentPriority returns the written slot 8, so both calls read the slot.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

int main(void) {
    volatile SceUInt32 *const slot1 = sceKernelGetTLSAddr(1);
    const SceUInt32 old1 = *slot1;
    const SceUInt32 written1 = ~old1;
    *slot1 = written1;
    const SceUID thread_id = sceKernelGetThreadId();
    *slot1 = old1;

    volatile SceUInt32 *const slot8 = sceKernelGetTLSAddr(8);
    const SceUInt32 old8 = *slot8;
    const SceUInt32 written8 = old8 + 1;
    *slot8 = written8;
    const int priority = sceKernelGetThreadCurrentPriority();
    *slot8 = old8;

    PRINT("slot 1 held the thread ID", old1 == (SceUInt32)sceKernelGetThreadId());
    PRINT("sceKernelGetThreadId returns the written slot 1", (SceUInt32)thread_id == written1);
    PRINT("slot 8 held the priority", old8 == (SceUInt32)sceKernelGetThreadCurrentPriority());
    PRINT("sceKernelGetThreadCurrentPriority returns the written slot 8", (SceUInt32)priority == written8);

    test_exit();
    return 0;
}
