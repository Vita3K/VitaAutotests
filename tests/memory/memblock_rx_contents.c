// What a new block of a read only or code type holds when the memory behind it was dirtied before.
// Each case fills a 1 MiB USER_RW block with 0x5A bytes and frees it, then makes a 1 MiB block of
// the type under test, scans it, and frees it. The types are a block for code from
// sceKernelAllocMemBlockForVM, USER_MAIN_R with no option struct, and USER_MAIN_R with an option
// struct of size 0x40 whose last word is 0, 1, 2 and 3. For each block the test prints whether
// every word is zero and whether a word holds the fill. The results are kept in variables and
// printed once every block is freed.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB 0x100000
#define FILL 0x5A5A5A5Au
#define CASES 6

// vitasdk has a stub for it but no prototype
int sceKernelFreeMemBlockForVM(SceUID uid);

typedef struct Opt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
    SceUInt32 extra[11];
} Opt;

static int made[CASES], all_zero[CASES], has_fill[CASES];

static void dirty(void) {
    const SceUID block = sceKernelAllocMemBlock("dirty", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MIB, NULL);
    SceUInt32 *words = NULL;
    sceKernelGetMemBlockBase(block, (void **)&words);
    for (unsigned i = 0; i < MIB / 4; i++)
        words[i] = FILL;
    sceKernelFreeMemBlock(block);
}

static void scan(int slot, SceUID block, int for_vm) {
    made[slot] = test_created(block);
    if (block < 0)
        return;
    const volatile SceUInt32 *words = NULL;
    sceKernelGetMemBlockBase(block, (void **)&words);
    all_zero[slot] = 1;
    for (unsigned i = 0; i < MIB / 4; i++) {
        if (words[i] != 0)
            all_zero[slot] = 0;
        if (words[i] == FILL)
            has_fill[slot] = 1;
    }
    if (for_vm)
        sceKernelFreeMemBlockForVM(block);
    else
        sceKernelFreeMemBlock(block);
}

int main(void) {
    dirty();
    scan(0, sceKernelAllocMemBlockForVM("for code", MIB), 1);
    dirty();
    scan(1, sceKernelAllocMemBlock("main r", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, MIB, NULL), 0);
    for (unsigned mode = 0; mode < 4; mode++) {
        Opt opt;
        sceClibMemset(&opt, 0, sizeof(opt));
        opt.size = 0x40;
        opt.extra[10] = mode;
        dirty();
        scan(2 + mode, sceKernelAllocMemBlock("main r", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, MIB, (SceKernelAllocMemBlockOpt *)&opt), 0);
    }

    static const char *const names[CASES] = { "a block for code", "USER_MAIN_R with no options", "USER_MAIN_R with word 15 = 0",
        "USER_MAIN_R with word 15 = 1", "USER_MAIN_R with word 15 = 2", "USER_MAIN_R with word 15 = 3" };
    for (int i = 0; i < CASES; i++) {
        PRINTF(made[i], "%s: allocate", names[i]);
        PRINTF(all_zero[i], "%s: every word is zero", names[i]);
        PRINTF(has_fill[i], "%s: a word holds the fill", names[i]);
    }
    test_exit();
    return 0;
}
