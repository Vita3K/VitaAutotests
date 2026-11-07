// Shared by the cpu_fetch_* tests. A worker thread calls a target address with r0 = 0x1234 set right
// before the call, and if the call returns, it records that and the r0 it returned with, signals a
// semaphore, then exits with 7. The main thread creates the worker and the semaphore first, sets the
// target up, starts the worker and waits on the semaphore. Only once the call returned does it print
// the result of a wait for the worker's end, the worker's exit status, its thread info, whether the
// call returned and with which r0, and a last line that shows it went on. So a call that never
// returns leaves the output at the line before the start.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static volatile SceUInt32 fetch_target;
static volatile int fetch_returned;
static volatile SceUInt32 fetch_r0;
static SceUID fetch_done;

// Calls fetch_target with r0 = 0x1234, without any call in between, and records that it returned
static void fetch_call(void) {
    register SceUInt32 r0 __asm__("r0") = 0x1234;
    register SceUInt32 r1 __asm__("r1") = fetch_target;
    __asm__ volatile("blx r1" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "lr", "memory", "cc");
    fetch_r0 = r0;
    fetch_returned = 1;
}

// The worker of every test but cpu_fetch_null_callback
__attribute__((unused)) static int fetch_worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    fetch_call();
    sceKernelSignalSema(fetch_done, 1);
    return 7;
}

static SceUID fetch_create_worker(SceKernelThreadEntry entry) {
    fetch_returned = 0;
    fetch_r0 = 0xDEAD;
    fetch_done = sceKernelCreateSema("fetch done", 0, 0, 1, NULL);
    return sceKernelCreateThread("worker", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
}

// Starts the worker on target, waits until its call returned, and prints what happened
static void fetch_run(SceUID worker, SceUInt32 target) {
    fetch_target = target;
    PRINT("release the worker", 1);
    const int start = sceKernelStartThread(worker, 0, NULL);
    if (start != 0) {
        PRINT("start the worker", start);
        return;
    }
    sceKernelWaitSema(fetch_done, 1, NULL);
    int status = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    PRINT("the wait for the worker's end", sceKernelWaitThreadEnd(worker, &status, &timeout));
    PRINT("its exit status", status);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINT("its thread info", sceKernelGetThreadInfo(worker, &info));
    PRINT("its status", info.status);
    PRINT("the call returned", fetch_returned);
    PRINT("r0 after the call", fetch_r0);
    PRINT("the main thread goes on", 1);
}
