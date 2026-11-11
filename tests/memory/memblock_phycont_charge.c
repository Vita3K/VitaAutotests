// How much main memory a USER_RW block takes with the PHYCONT attribute (0x200000), for sizes that
// are not a power of two, next to the same sizes without it. The main free size is measured to 4
// KiB with 4 KiB probes, the way app_free_sizes does. For each size and each form the test prints
// the KiB the block takes, the mapped size that the info call reports for it, and whether the free
// size is back once the block is freed. Everything is measured first and printed after.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define PHYCONT 0x200000
#define PROBES 320
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static SceUID probes[PROBES];

static int reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_user;
}

static int measure(void) {
    const int start = reported();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * 4 * KIB;
}

int main(void) {
    static const SceSize sizes[] = { 0x3000, 0x21000, 0x81000, MIB + 0x1000, 3 * MIB + 0x1000 };
    int made[2][COUNT(sizes)], taken[2][COUNT(sizes)], back[2][COUNT(sizes)];
    unsigned mapped[2][COUNT(sizes)];
    measure();
    for (int form = 0; form < 2; form++) {
        for (unsigned i = 0; i < COUNT(sizes); i++) {
            AllocOpt opt = { .size = 0x14, .attr = form ? PHYCONT : 0 };
            const int before = measure();
            const SceUID block = sceKernelAllocMemBlock("phycont", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sizes[i], (SceKernelAllocMemBlockOpt *)&opt);
            made[form][i] = test_created(block);
            taken[form][i] = 0;
            mapped[form][i] = 0;
            if (block >= 0) {
                taken[form][i] = before - measure();
                void *base = NULL;
                SceKernelMemBlockInfo info = { .size = sizeof(info) };
                sceKernelGetMemBlockBase(block, &base);
                sceKernelGetMemBlockInfoByAddr(base, &info);
                mapped[form][i] = info.mappedSize;
                sceKernelFreeMemBlock(block);
            }
            back[form][i] = measure() == before;
        }
    }
    for (int form = 0; form < 2; form++) {
        for (unsigned i = 0; i < COUNT(sizes); i++) {
            const char *kind = form ? "with PHYCONT" : "without PHYCONT";
            PRINTF(made[form][i], "0x%X bytes %s: allocate", (unsigned)sizes[i], kind);
            PRINTF(taken[form][i] / KIB, "0x%X bytes %s: the KiB it takes", (unsigned)sizes[i], kind);
            PRINTF(mapped[form][i], "0x%X bytes %s: the mapped size", (unsigned)sizes[i], kind);
            PRINTF(back[form][i], "0x%X bytes %s: the free size is back after the free", (unsigned)sizes[i], kind);
        }
    }
    test_exit();
    return 0;
}
