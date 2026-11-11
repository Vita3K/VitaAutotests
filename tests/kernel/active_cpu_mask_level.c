// What sceKernelChangeActiveCpuMask gives an app. The call is made with 0x100000, which is no valid
// core mask, so a call that passes the permission check fails on the mask and changes nothing. It is
// made from the main thread and from a created thread, and the results are printed. 0x80010058
// means the permission check refused the call, and 0x80028025 means it passed and the mask was
// refused.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The stub library has it, but no header declares it
SceInt32 sceKernelChangeActiveCpuMask(SceUInt32 mask);

static int thread_result;

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    thread_result = sceKernelChangeActiveCpuMask(0x100000);
    return 0;
}

int main(void) {
    const int main_result = sceKernelChangeActiveCpuMask(0x100000);
    const SceUID t = test_start_thread(t_entry);
    sceKernelWaitThreadEnd(t, NULL, NULL);
    sceKernelDeleteThread(t);
    PRINT("the main thread calls it with 0x100000", main_result);
    PRINT("a created thread calls it with 0x100000", thread_result);
    test_exit();
    return 0;
}
