// sceKernelFreeMemBlockForVM on UIDs that aren't a block for code. Model: memblock_uid_kinds, whose
// not_a_block cases make the usual calls on the same kinds of UID. Here a 1 MiB block for code
// stays live while the call for code's free is made once on each of these, in order: a 4 KiB plain
// block, a 64 KiB USER_RW block with ENABLE_OPEN, a semaphore, a mutex, the UID 0x12345, UID 0 and
// UID -1. Then the plain block's base is looked up, and a semaphore signal and a mutex lock are
// tried. Last, a plain or ENABLE_OPEN block the call refused is freed with the usual free, and the
// block for code is freed with the call for code. A block the call took is gone already, so it
// isn't freed again.
//
// The cases print each call's result. A find prints 0 when it gives the plain block, the error when
// it fails and 1 when it gives another UID.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

// The call for code's free, which the shared declarations lack
int sceKernelFreeMemBlockForVM(SceUID uid);

static void *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Returns 0 when base is in block, the error when no block is, and 1 when another UID is
static int find_result(void *base, SceUID block) {
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    return found == block ? 0 : found < 0 ? found : 1;
}

int main(void) {
    const SceUID for_vm = sceKernelAllocMemBlockForVM("vm uid", MIB);
    PRINT("allocate the block for code", test_created(for_vm));

    const SceUID plain =
        sceKernelAllocMemBlock("uid plain", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    PRINT("allocate the 4 KiB plain block", test_created(plain));
    void *plain_base = base_of(plain);

    AllocOpt opt = {.size = sizeof(opt), .attr = ENABLE_OPEN};
    const SceUID enable = sceKernelAllocMemBlock("uid open", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                 64 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the 64 KiB ENABLE_OPEN block", test_created(enable));

    const SceUID sema = sceKernelCreateSema("uid sema", 0, 0, 1, NULL);
    const SceUID mutex = sceKernelCreateMutex("uid mutex", 0, 0, NULL);

    const int plain_for_vm = sceKernelFreeMemBlockForVM(plain);
    PRINT("the call for code on the 4 KiB plain block", plain_for_vm);
    const int enable_for_vm = sceKernelFreeMemBlockForVM(enable);
    PRINT("the call for code on the 64 KiB ENABLE_OPEN block", enable_for_vm);
    PRINT("the call for code on a semaphore", sceKernelFreeMemBlockForVM(sema));
    PRINT("the call for code on a mutex", sceKernelFreeMemBlockForVM(mutex));
    PRINT("the call for code on an unknown UID", sceKernelFreeMemBlockForVM(0x12345));
    PRINT("the call for code on UID 0", sceKernelFreeMemBlockForVM(0));
    PRINT("the call for code on UID -1", sceKernelFreeMemBlockForVM(-1));

    PRINT("the plain block's base still finds its UID", find_result(plain_base, plain));
    PRINT("the semaphore still works after them", sceKernelSignalSema(sema, 1));
    PRINT("the mutex still works after them", sceKernelLockMutex(mutex, 1, NULL));
    sceKernelUnlockMutex(mutex, 1);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(sema);

    // A block the call refused still needs the usual free
    if (plain_for_vm < 0 && plain >= 0)
        PRINT("the 4 KiB plain block, freed with the usual free", sceKernelFreeMemBlock(plain));
    if (enable_for_vm < 0 && enable >= 0)
        PRINT("the 64 KiB ENABLE_OPEN block, freed with the usual free",
              sceKernelFreeMemBlock(enable));
    PRINT("the block for code, freed with the call for code", sceKernelFreeMemBlockForVM(for_vm));
    test_exit();
    return 0;
}
