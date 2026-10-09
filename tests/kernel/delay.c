// Delays and timeouts last at least as long as asked, and not much longer
#include <psp2/kernel/error.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

// Generous, so a slow system doesn't fail the test
#define MAX_OVERSHOOT_US 200000

int main(void) {
    SceUInt64 start = sceKernelGetProcessTimeWide();
    sceKernelDelayThread(20000);
    SceUInt64 elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("a delay lasts at least as long as asked", elapsed >= 20000);
    PRINT("a delay doesn't last much longer", elapsed < 20000 + MAX_OVERSHOOT_US);

    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    SceUInt timeout = 20000;
    start = sceKernelGetProcessTimeWide();
    sceKernelWaitSema(sema, 1, &timeout);
    elapsed = sceKernelGetProcessTimeWide() - start;
    PRINT("a timeout lasts at least as long as asked", elapsed >= 20000);
    PRINT("a timeout doesn't last much longer", elapsed < 20000 + MAX_OVERSHOOT_US);

    test_exit();
    return 0;
}
