// Info by UID of a lightweight mutex whose work area block was freed. The mutex lives on in the
// kernel, but its work area is in no block, so the info can't read the owner and count from it. The
// app makes the mutex in a block, reads its info by its work area while the block is mapped, frees
// the block, then reads the info by UID with its count and owner set beforehand. The mutex is not
// deleted, since a delete by its work area would read the freed memory. This is kept apart from
// lw_work_area_memory, since an implementation may crash on the read of freed memory. The cases:
// - the create and the info by work area of the mutex, before the block is freed
// - the info by UID after the free: its result, its count, and whether its owner is -1
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define KIB 0x400
#define WORK_OFFSET 0x40

int main(void) {
    const SceUID block = sceKernelAllocMemBlock("lw freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        64 * KIB, NULL);
    unsigned char *base = NULL;
    sceKernelGetMemBlockBase(block, (void **)&base);
    SceKernelLwMutexWork *work = (SceKernelLwMutexWork *)(void *)(base + WORK_OFFSET);

    PRINT("create in the block", sceKernelCreateLwMutex(work, "m", 0, 0, NULL));
    LwMutexInfo made = { .size = sizeof(made) };
    PRINT("info by its work area, before the block is freed",
        sceKernelGetLwMutexInfo(work, &made));
    sceKernelFreeMemBlock(block);

    LwMutexInfo after = { .size = sizeof(after), .currentCount = 0x1234,
        .currentOwnerId = 0x5678 };
    PRINT("info by UID after the block is freed", sceKernelGetLwMutexInfoById(made.uid, &after));
    PRINT("info by UID after the block is freed: the count", after.currentCount);
    PRINT("info by UID after the block is freed: the owner is -1", after.currentOwnerId == -1);

    test_exit();
    return 0;
}
