// The openable attribute, 0x80, on lightweight condition variables and mutexes, with controls
// made with other attributes. The cases:
// - a lightweight condition variable with attribute 0x80, and with 0x80|0x2000
// - a lightweight condition variable with attribute 0x2000 (the control)
// - a lightweight mutex with attribute 0x80, and with 0 (the control)
// For a refused creation the test prints the result and whether the work area changed, which it
// fills with a marker first. For the condition variable control it prints each call and whether
// the control works: the condition variable is signaled, then waited on with a timeout of 0 while
// its mutex is locked, and then the mutex is unlocked. The lightweight mutex control is locked and
// unlocked. Each object that was made is deleted. The model lw_cond_errors covers the other
// attribute errors of lightweight condition variables.
#include <psp2/kernel/error.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define OPENABLE 0x80
#define PRIORITY_ORDER 0x2000

// Whether every byte of a work area still holds the marker it was filled with
static int untouched(const void *work, SceSize size) {
    const unsigned char *bytes = work;
    for (SceSize i = 0; i < size; i++)
        if (bytes[i] != 0xCC)
            return 0;
    return 1;
}

// Tries a lightweight condition variable with attr on mutex. A refused creation also prints whether
// its work area changed, and a creation that works is deleted again.
static void refused_cond(const char *label, SceUInt32 attr, SceKernelLwMutexWork *mutex) {
    static SceKernelLwCondWork work;
    sceClibMemset(&work, 0xCC, sizeof(work));
    const int result = sceKernelCreateLwCond(&work, "cond", attr, mutex, NULL);
    PRINTF(result, "lightweight condition variable with %s: create", label);
    if (result < 0)
        PRINTF(!untouched(&work, sizeof(work)),
               "lightweight condition variable with %s: the work area changed", label);
    else
        sceKernelDeleteLwCond(&work);
}

int main(void) {
    static SceKernelLwMutexWork mutex;
    PRINT("create the lightweight mutex for the condition variables",
          sceKernelCreateLwMutex(&mutex, "mutex", 0, 0, NULL));

    refused_cond("attribute 0x80", OPENABLE, &mutex);
    refused_cond("attribute 0x80|0x2000", OPENABLE | PRIORITY_ORDER, &mutex);

    static SceKernelLwCondWork control;
    const int control_result =
        sceKernelCreateLwCond(&control, "control", PRIORITY_ORDER, &mutex, NULL);
    PRINT("lightweight condition variable with attribute 0x2000 (control): create", control_result);
    if (control_result == 0) {
        const int signal = sceKernelSignalLwCond(&control);
        PRINT("control condition variable: signal with nobody waiting", signal);
        const int lock = sceKernelLockLwMutex(&mutex, 1, NULL);
        PRINT("control condition variable: lock its mutex", lock);
        SceUInt32 timeout = 0;
        const int wait = sceKernelWaitLwCond(&control, &timeout);
        PRINT("control condition variable: wait with a timeout of 0", wait);
        const int unlock = sceKernelUnlockLwMutex(&mutex, 1);
        PRINT("control condition variable: unlock its mutex after the wait", unlock);
        PRINT("control condition variable works", signal == 0 && lock == 0 &&
                                                      wait == (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT &&
                                                      unlock == 0);
        sceKernelDeleteLwCond(&control);
    }

    static SceKernelLwMutexWork refused;
    sceClibMemset(&refused, 0xCC, sizeof(refused));
    const int refused_result = sceKernelCreateLwMutex(&refused, "refused", OPENABLE, 0, NULL);
    PRINT("lightweight mutex with attribute 0x80: create", refused_result);
    if (refused_result < 0)
        PRINT("lightweight mutex with attribute 0x80: the work area changed",
              !untouched(&refused, sizeof(refused)));
    else
        sceKernelDeleteLwMutex(&refused);

    static SceKernelLwMutexWork lw_control;
    const int lw_control_result = sceKernelCreateLwMutex(&lw_control, "control", 0, 0, NULL);
    PRINT("lightweight mutex control: create", lw_control_result);
    if (lw_control_result == 0) {
        const int lock = sceKernelLockLwMutex(&lw_control, 1, NULL);
        const int unlock = sceKernelUnlockLwMutex(&lw_control, 1);
        PRINT("lightweight mutex control: lock", lock);
        PRINT("lightweight mutex control: unlock", unlock);
        PRINT("lightweight mutex control works", lock == 0 && unlock == 0);
        sceKernelDeleteLwMutex(&lw_control);
    }

    sceKernelDeleteLwMutex(&mutex);
    test_exit();
    return 0;
}
