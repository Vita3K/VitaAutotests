// sceKernelSyncVMDomain on ranges of memory blocks of several kinds: a plain 4 KiB USER_RW block, a
// 0x10 alias of an ENABLE_OPEN 4 KiB block, and a 256 KiB USER_CDRAM_RW block. The cases:
// - the plain block, a range of 2 bytes at its last byte, which runs past its end
// - the plain block, a range of 4 bytes starting at its end
// - the alias, a range of 8 bytes inside it at 0x800
// - the CDRAM block, a range of 8 bytes inside it at 0x1000
// memblock_for_vm syncs a whole data block and a range that runs past the end of a code block, with
// the domain closed. Here the domain stays closed throughout. Every UID passed is one the test
// made, since a sync with an unknown UID never returns, and no range is 0 bytes long, since a sync
// of 0 bytes never returns either. A block that wasn't made skips its cases, and its allocation
// result is printed in its place. The blocks are freed or closed at the end.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static void *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

int main(void) {
    const SceUID plain =
        sceKernelAllocMemBlock("sync plain", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    PRINT("allocate the plain block", test_created(plain));
    if (plain >= 0) {
        char *base = base_of(plain);
        PRINT("sync the plain block, 2 bytes at its last byte",
              sceKernelSyncVMDomain(plain, base + 4 * KIB - 1, 2));
        PRINT("sync the plain block, 4 bytes at its end",
              sceKernelSyncVMDomain(plain, base + 4 * KIB, 4));
    }

    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("sync alias", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                   4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the openable block", test_created(original));
    const SceUID alias = original >= 0 ? sceKernelOpenMemBlock("sync alias", 0x10) : original;
    PRINT("open it with 0x10", test_created(alias));
    if (alias >= 0) {
        PRINT("sync the alias, 8 bytes at 0x800",
              sceKernelSyncVMDomain(alias, (char *)base_of(alias) + 0x800, 8));
        sceKernelCloseMemBlock(alias);
    }
    if (original >= 0)
        sceKernelFreeMemBlock(original);

    const SceUID cdram = sceKernelAllocMemBlock(
        "sync cdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, NULL);
    PRINT("allocate the CDRAM block", test_created(cdram));
    if (cdram >= 0) {
        PRINT("sync the CDRAM block, 8 bytes at 0x1000",
              sceKernelSyncVMDomain(cdram, (char *)base_of(cdram) + 0x1000, 8));
        sceKernelFreeMemBlock(cdram);
    }

    if (plain >= 0)
        sceKernelFreeMemBlock(plain);

    test_exit();
    return 0;
}
