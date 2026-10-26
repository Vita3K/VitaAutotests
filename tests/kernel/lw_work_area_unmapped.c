// Lightweight mutex create and delete given a work area in no memory block. The area is the base
// of a 64 KiB USER_RW block freed right before the calls, as in unmapped_in, and the test first
// checks with sceKernelFindMemBlockByAddr that no block maps it. The app never locks, unlocks or
// reads the info of that area. This is kept apart from lw_work_area_memory, since an implementation
// may crash on a create or delete in unmapped memory. The cases:
// - the check that the base is in no block
// - a create with the base as the work area, which is deleted if it was made
// - a delete with the base as the work area
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define KIB 0x400

// Allocates a block and frees it, and returns its base, which is in no block until the next
// allocation
static unsigned char *unmapped(void) {
    const SceUID block
        = sceKernelAllocMemBlock("lw unmapped", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB, NULL);
    unsigned char *base = NULL;
    sceKernelGetMemBlockBase(block, (void **)&base);
    sceKernelFreeMemBlock(block);
    return base;
}

int main(void) {
    SceKernelLwMutexWork *work = (SceKernelLwMutexWork *)(void *)unmapped();
    PRINT("the base of a freed block is in no block", sceKernelFindMemBlockByAddr(work, 0));
    PRINT("create with the base as the work area", sceKernelCreateLwMutex(work, "m", 0, 0, NULL));
    PRINT("delete with the base as the work area", sceKernelDeleteLwMutex(work));

    test_exit();
    return 0;
}
