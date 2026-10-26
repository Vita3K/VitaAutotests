// The sizes sceKernelAllocMemBlock takes for three types, and how the mapped size of a block it
// makes relates to the size asked for. Each type is allocated at the sizes below, and for a block
// that is made, sceKernelGetMemBlockInfoByAddr gives its mapped size, which is printed as a class:
// 0 if it is the size asked for, 1 if it is that size rounded up to the type's unit, and 2 for
// anything else. The units are 4 KiB for USER_RW, 256 KiB for USER_CDRAM_RW and 1 MiB for
// USER_MAIN_PHYCONT_RW, the unit of the one allocation memblock.c makes of each. The cases:
// - USER_RW of 0x800, 0x1001, 0x1800 and 0x2000 bytes
// - USER_CDRAM_RW of 0x20000, 0x41000, 0x44000 and 0x80000 bytes
// - USER_MAIN_PHYCONT_RW of 0x80000, 0x101000, 0x180000 and 0x200000 bytes
// Every block that is made is freed before the next one, and an info call that fails prints its
// error in place of the class.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

static const struct {
    const char *name;
    SceUInt32 type;
    SceSize unit;
    SceSize sizes[4];
} kinds[] = {
    { "USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, { 0x800, 0x1001, 0x1800, 0x2000 } },
    { "USER_CDRAM_RW",
      SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
      256 * KIB,
      { 0x20000, 0x41000, 0x44000, 0x80000 } },
    { "USER_MAIN_PHYCONT_RW",
      SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW,
      MIB,
      { 0x80000, 0x101000, 0x180000, 0x200000 } },
};

// Returns 0 if mapped is the size, 1 if it is the size rounded up to the unit, else 2
static int size_class(SceSize size, SceSize unit, SceSize mapped) {
    const SceSize rounded = (size + unit - 1) / unit * unit;
    if (mapped == size)
        return 0;
    return mapped == rounded ? 1 : 2;
}

// Allocates one block of the size, prints the result and the class of its mapped size
static void check_size(const char *name, SceUInt32 type, SceSize unit, SceSize size) {
    const SceUID block = sceKernelAllocMemBlock("granule", type, size, NULL);
    PRINTF(test_created(block), "%s, 0x%X bytes: allocate", name, (unsigned)size);
    if (block < 0)
        return;
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    const int error = sceKernelGetMemBlockInfoByAddr(base, &info);
    if (error < 0)
        PRINTF(error, "%s, 0x%X bytes: info by address", name, (unsigned)size);
    else
        PRINTF(size_class(size, unit, info.mappedSize), "%s, 0x%X bytes: mapped size class", name,
               (unsigned)size);
    sceKernelFreeMemBlock(block);
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
        for (unsigned int j = 0; j < sizeof(kinds[i].sizes) / sizeof(kinds[i].sizes[0]); j++)
            check_size(kinds[i].name, kinds[i].type, kinds[i].unit, kinds[i].sizes[j]);

    test_exit();
    return 0;
}
