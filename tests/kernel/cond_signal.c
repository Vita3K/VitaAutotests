// Condition variable signals with nobody waiting
#include <psp2/kernel/threadmgr.h>

#include "test.h"

int main(void) {
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    SceUInt timeout;

    PRINT("signal with nobody waiting", sceKernelSignalCond(cond));
    PRINT("signal all with nobody waiting", sceKernelSignalCondAll(cond));
    sceKernelLockMutex(mutex, 1, NULL);
    timeout = 1000;
    PRINT("a wait after those signals", sceKernelWaitCond(cond, &timeout));
    timeout = 0;
    PRINT("a wait with a timeout of 0", sceKernelWaitCond(cond, &timeout));
    PRINT("the mutex is owned after it", sceKernelUnlockMutex(mutex, 1));
    PRINT("signal while holding the mutex", sceKernelSignalCond(cond));
    PRINT("delete with nobody waiting", sceKernelDeleteCond(cond));
    PRINT("signal a deleted condition", sceKernelSignalCond(cond));
    PRINT("the mutex outlives the condition", sceKernelTryLockMutex(mutex, 1));

    test_exit();
    return 0;
}
