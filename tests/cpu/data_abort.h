// Shared by the cpu_data_abort_* tests. The worker of fetch_fault.h calls a function that loads from
// or stores to data_abort_address, which a test sets before the call.
#include "fetch_fault.h"

static volatile SceUInt32 *volatile data_abort_address;

__attribute__((unused)) static void data_abort_load(void) {
    fetch_r0 = *data_abort_address;
}

__attribute__((unused)) static void data_abort_store(void) {
    *data_abort_address = 0x5555;
}

// Returns the base of a block that was allocated and freed
__attribute__((unused)) static void *data_abort_freed_base(void) {
    const SceUID block = sceKernelAllocMemBlock("freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    PRINT("allocate the block", block > 0);
    void *base = NULL;
    PRINT("its base", sceKernelGetMemBlockBase(block, &base));
    PRINT("free it", sceKernelFreeMemBlock(block));
    PRINT("no block holds its base", sceKernelFindMemBlockByAddr(base, 0) < 0);
    return base;
}
