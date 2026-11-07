// A call in ARM state to code that the same thread already ran from a block for code, after the
// block was freed, with the harness of fetch_fault.h. The code returns 0x55: mov r0, #0x55, then
// bx lr. The worker calls it once, then waits until the main thread freed the block, and calls the
// same address again. A thread that runs code keeps what it ran, so only the second call of the
// same thread shows whether the free took the code away.
//
// On a PS Vita TV on firmware 3.65 the first call returns 0x55, and the process ends with a core dump
// at the second call, so the output stops at the release line.
#include "fetch_fault.h"

#define MIB 0x100000

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

static SceUID ran_once, freed;
static volatile SceUInt32 first_r0;

static int twice_worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    fetch_call();
    first_r0 = fetch_r0;
    fetch_returned = 0;
    sceKernelSignalSema(ran_once, 1);
    sceKernelWaitSema(freed, 1, NULL);
    fetch_call();
    sceKernelSignalSema(fetch_done, 1);
    return 7;
}

int main(void) {
    const SceUID worker = fetch_create_worker(twice_worker);
    PRINT("create the worker", worker > 0);
    ran_once = sceKernelCreateSema("ran once", 0, 0, 1, NULL);
    freed = sceKernelCreateSema("freed", 0, 0, 1, NULL);
    const SceUID block = sceKernelAllocMemBlockForVM("code", MIB);
    PRINT("allocate the block", block > 0);
    void *base = NULL;
    PRINT("its base", sceKernelGetMemBlockBase(block, &base));
    sceKernelOpenVMDomain();
    volatile SceUInt32 *words = base;
    words[0] = 0xE3A00055;
    words[1] = 0xE12FFF1E;
    sceKernelCloseVMDomain();
    PRINT("sync the code", sceKernelSyncVMDomain(block, base, 2 * sizeof(SceUInt32)));
    fetch_target = (SceUInt32)base;
    PRINT("start the worker", sceKernelStartThread(worker, 0, NULL));
    SceUInt timeout = TEST_WAIT_US;
    PRINT("the first call returned", sceKernelWaitSema(ran_once, 1, &timeout));
    PRINT("r0 after the first call", first_r0);
    PRINT("free the block", sceKernelFreeMemBlockForVM(block));
    PRINT("no block holds its base", sceKernelFindMemBlockByAddr(base, 0) < 0);
    PRINT("release the worker", 1);
    sceKernelSignalSema(freed, 1);
    sceKernelWaitSema(fetch_done, 1, NULL);
    int status = 0xDEAD;
    timeout = TEST_WAIT_US;
    PRINT("the wait for the worker's end", sceKernelWaitThreadEnd(worker, &status, &timeout));
    PRINT("its exit status", status);
    PRINT("the second call returned", fetch_returned);
    PRINT("r0 after the second call", fetch_r0);
    PRINT("the main thread goes on", 1);
    test_exit();
    return 0;
}
