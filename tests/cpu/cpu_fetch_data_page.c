// A call in ARM state to code written into an ordinary read and write block, with the harness of
// fetch_fault.h. The code returns 0x55: mov r0, #0x55, then bx lr. The block is synced with
// sceKernelSyncVMDomain, and the result of that is printed too. cpu_fetch_vm_block runs the same
// code from a block for code.
//
// On a PS Vita TV on firmware 3.65 the sync returns 0, and the process ends with a core dump when
// the worker makes the call, so code in a read and write block doesn't run. The output stops at the
// release line.
#include "fetch_fault.h"

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    const SceUID block = sceKernelAllocMemBlock("data", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    PRINT("allocate the block", block > 0);
    void *base = NULL;
    PRINT("its base", sceKernelGetMemBlockBase(block, &base));
    volatile SceUInt32 *words = base;
    words[0] = 0xE3A00055;
    words[1] = 0xE12FFF1E;
    PRINT("sync the code", sceKernelSyncVMDomain(block, base, 2 * sizeof(SceUInt32)));
    fetch_run(worker, (SceUInt32)base);
    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
