// The process and system clocks: whether they advance with a delay, agree with each other and with
// their narrower variants. Every time is printed as a comparison.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

// True if a and b are at most limit apart
static int close_to(SceInt64 a, SceInt64 b, SceInt64 limit) {
    const SceInt64 diff = a > b ? a - b : b - a;
    return diff <= limit;
}

int main(void) {
    // Process time
    const SceUInt64 process_before = sceKernelGetProcessTimeWide();
    sceKernelDelayThread(20000);
    const SceUInt64 process_after = sceKernelGetProcessTimeWide();
    PRINT("process time advances by at least a 20 ms delay", process_after - process_before >= 20000);
    PRINT("and by less than a second", process_after - process_before < 1000000);
    SceKernelSysClock clock = 0;
    PRINT("get the process time", sceKernelGetProcessTime(&clock));
    PRINT("it matches the wide one", close_to((SceInt64)clock, (SceInt64)sceKernelGetProcessTimeWide(), 10000));
    const SceUInt32 low = sceKernelGetProcessTimeLow();
    const SceUInt32 wide_low = (SceUInt32)sceKernelGetProcessTimeWide();
    PRINT("the low one is the low word of the wide one", wide_low - low < 10000);

    // System time
    const SceUInt64 system_before = (SceUInt64)sceKernelGetSystemTimeWide();
    sceKernelDelayThread(20000);
    const SceUInt64 system_after = (SceUInt64)sceKernelGetSystemTimeWide();
    PRINT("system time advances by at least a 20 ms delay", system_after - system_before >= 20000);
    PRINT("and by less than a second", system_after - system_before < 1000000);
    PRINT("system time is ahead of process time", (SceUInt64)sceKernelGetSystemTimeWide() > sceKernelGetProcessTimeWide());

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("get the process time into a NULL pointer", sceKernelGetProcessTime(NULL));

    test_exit();
    return 0;
}
