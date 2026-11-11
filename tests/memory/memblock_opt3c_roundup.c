// The attr bit 0x2000 with option structs of size 0x38, 0x3C and 0x40, for 4 KiB of USER_RW with a
// rounding of 0x10000 in word 14. Each case prints the result, and for a block that is made, its
// base's alignment to 0x10000 and its mapped size. The option struct of size 0x40 comes first as the
// control.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define ROUNDUP 0x2000
#define CASES 3

typedef struct Opt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
    SceUInt32 extra[11];
} Opt;

int main(void) {
    static const SceSize opt_sizes[CASES] = { 0x40, 0x3C, 0x38 };
    int results[CASES], aligned[CASES];
    unsigned sizes[CASES];
    for (int i = 0; i < CASES; i++) {
        Opt opt;
        sceClibMemset(&opt, 0, sizeof(opt));
        opt.size = opt_sizes[i];
        opt.attr = ROUNDUP;
        opt.extra[9] = 0x10000;
        const SceUID block = sceKernelAllocMemBlock("roundup", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, (SceKernelAllocMemBlockOpt *)&opt);
        results[i] = test_created(block);
        aligned[i] = 0;
        sizes[i] = 0;
        if (block >= 0) {
            void *base = NULL;
            SceKernelMemBlockInfo info = { .size = sizeof(info) };
            sceKernelGetMemBlockBase(block, &base);
            sceKernelGetMemBlockInfoByAddr(base, &info);
            aligned[i] = ((SceUIntPtr)base & 0xFFFF) == 0;
            sizes[i] = info.mappedSize;
            sceKernelFreeMemBlock(block);
        }
    }
    for (int i = 0; i < CASES; i++) {
        PRINTF(results[i], "option size 0x%X: allocate", (unsigned)opt_sizes[i]);
        PRINTF(aligned[i], "option size 0x%X: the base is 64 KiB aligned", (unsigned)opt_sizes[i]);
        PRINTF(sizes[i], "option size 0x%X: the mapped size", (unsigned)opt_sizes[i]);
    }
    test_exit();
    return 0;
}
