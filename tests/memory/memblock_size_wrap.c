// sceKernelAllocMemBlock with USER_RW sizes near the top of the 32-bit range: 0x80000000,
// 0xFFFFF000, 0xFFFFF001 and 0xFFFFFFFF, in that order. For each size the result is printed, a
// block that is made is freed, and then the free sizes are compared with those from before the
// first call. The free sizes are read into variables during the test and printed at the end, so the
// output doesn't take memory in between the reads. This test stands alone, since a wrong
// implementation may crash on a size this big, and no other test should lose its output to that.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static SceKernelFreeMemorySizeInfo free_sizes(void) {
    SceKernelFreeMemorySizeInfo info = { .size = sizeof(info) };
    sceKernelGetFreeMemorySize(&info);
    return info;
}

static int same_sizes(const SceKernelFreeMemorySizeInfo *a, const SceKernelFreeMemorySizeInfo *b) {
    return a->size_user == b->size_user && a->size_cdram == b->size_cdram &&
           a->size_phycont == b->size_phycont;
}

int main(void) {
    static const SceSize sizes[] = { 0x80000000, 0xFFFFF000, 0xFFFFF001, 0xFFFFFFFF };
    const SceKernelFreeMemorySizeInfo before = free_sizes();
    SceUID results[COUNT(sizes)];
    int same[COUNT(sizes)];
    for (unsigned int i = 0; i < COUNT(sizes); i++) {
        const SceUID block =
            sceKernelAllocMemBlock("size wrap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sizes[i], NULL);
        results[i] = test_created(block);
        if (block >= 0)
            sceKernelFreeMemBlock(block);
        const SceKernelFreeMemorySizeInfo after = free_sizes();
        same[i] = same_sizes(&before, &after);
    }
    for (unsigned int i = 0; i < COUNT(sizes); i++) {
        PRINTF(results[i], "USER_RW, 0x%X bytes: allocate", (unsigned)sizes[i]);
        PRINTF(same[i], "USER_RW, 0x%X bytes: the free sizes are the same as before",
               (unsigned)sizes[i]);
    }

    test_exit();
    return 0;
}
