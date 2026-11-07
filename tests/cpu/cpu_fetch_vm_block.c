// A call in ARM state to code written into a block for code, with the harness of fetch_fault.h, the
// control case of cpu_fetch_data_page. The code returns 0x55: mov r0, #0x55, then bx lr. It is
// written with the domain open, and the range is synced after the domain is closed.
//
// On a PS Vita TV on firmware 3.65 the call returns 0x55, and the worker exits with 7.
#include "fetch_fault.h"

#define MIB 0x100000

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
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
    fetch_run(worker, (SceUInt32)base);
    sceKernelFreeMemBlockForVM(block);
    test_exit();
    return 0;
}
