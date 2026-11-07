// A call in ARM state to the base of a block that was freed, with the harness of fetch_fault.h. The
// worker and the semaphore are created first, and nothing is allocated between the free and the call.
//
// On a PS Vita TV on firmware 3.65 the process ends with a core dump when the worker makes the call,
// so the output stops at the release line.
#include "fetch_fault.h"

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    const SceUID block = sceKernelAllocMemBlock("freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    PRINT("allocate the block", block > 0);
    void *base = NULL;
    PRINT("its base", sceKernelGetMemBlockBase(block, &base));
    PRINT("free it", sceKernelFreeMemBlock(block));
    PRINT("no block holds its base", sceKernelFindMemBlockByAddr(base, 0) < 0);
    fetch_run(worker, (SceUInt32)base);
    test_exit();
    return 0;
}
