// What a USER_CDRAM_RW allocation of 256 KiB keeps when the option check refuses it. The cases:
// - attr 0x1000000 alone, with an option struct of size 0x14
// - option struct of size 0x38 with attr 0x20
// - option struct of size 0x40 with attr 0x2000 and word 14 at 0x2000, which is no valid rounding
// - a plain allocation after them, as the control
// For each the test prints the result and the KiB that the CDRAM free size dropped by across the
// call, measured to 256 KiB with USER_CDRAM_RW probes as app_free_sizes does. A block that is made
// is freed after its drop is measured. The results are printed after every case ran.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define UNIT (256 * KIB)
#define PROBES 320
#define CASES 4

typedef struct Opt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
    SceUInt32 extra[11];
} Opt;

static SceUID probes[PROBES];

static int reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_cdram;
}

static int measure(void) {
    const int start = reported();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, UNIT, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * UNIT;
}

int main(void) {
    static const char *const names[CASES] = { "attr 0x1000000, option size 0x14", "attr 0x20, option size 0x38",
        "attr 0x2000 with word 14 at 0x2000, option size 0x40", "no options" };
    int results[CASES], dropped[CASES];
    measure();
    for (int i = 0; i < CASES; i++) {
        Opt opt;
        sceClibMemset(&opt, 0, sizeof(opt));
        if (i == 0) {
            opt.size = 0x14;
            opt.attr = 0x1000000;
        } else if (i == 1) {
            opt.size = 0x38;
            opt.attr = 0x20;
        } else if (i == 2) {
            opt.size = 0x40;
            opt.attr = 0x2000;
            opt.extra[9] = 0x2000;
        }
        const int before = measure();
        const SceUID block = sceKernelAllocMemBlock("cdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, UNIT, i < 3 ? (SceKernelAllocMemBlockOpt *)&opt : NULL);
        results[i] = test_created(block);
        dropped[i] = before - measure();
        if (block >= 0)
            sceKernelFreeMemBlock(block);
    }
    for (int i = 0; i < CASES; i++) {
        PRINTF(results[i], "%s: allocate", names[i]);
        PRINTF(dropped[i] / KIB, "%s: the KiB the CDRAM free size dropped by", names[i]);
    }
    test_exit();
    return 0;
}
