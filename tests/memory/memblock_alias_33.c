// What freeing an original unmaps when it has more than 32 live aliases. An ENABLE_OPEN 64 KiB
// block E has 33 aliases opened with 0x30. Each is looked up by its base. Then E is freed, and
// each alias is looked up again. The test prints whether every open worked, and the lookups: for
// the first 32 aliases how many are found after the free, and for the 33rd its lookup before and
// after. The aliases are closed at the end. A find prints 0 when it gives the block, the error
// when it fails and 1 when it gives another UID.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define ALIASES 33
#define ENABLE_OPEN 0x4000

typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static int find_result(const void *base, SceUID block) {
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    return found == block ? 0 : found < 0 ? found : 1;
}

int main(void) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("alias many", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000, (SceKernelAllocMemBlockOpt *)&opt);
    SceUID alias[ALIASES];
    void *base[ALIASES] = { NULL };
    int open_ok = 1, last_open = 0;
    for (int i = 0; i < ALIASES; i++) {
        alias[i] = sceKernelOpenMemBlock("alias many", 0x30);
        open_ok &= alias[i] >= 0;
        if (i == ALIASES - 1)
            last_open = test_created(alias[i]);
        if (alias[i] >= 0)
            sceKernelGetMemBlockBase(alias[i], &base[i]);
    }
    int found_before = 0;
    for (int i = 0; i < ALIASES - 1; i++)
        found_before += find_result(base[i], alias[i]) == 0;
    const int last_before = find_result(base[ALIASES - 1], alias[ALIASES - 1]);
    const int free_original = sceKernelFreeMemBlock(original);
    int found_after = 0;
    for (int i = 0; i < ALIASES - 1; i++)
        found_after += find_result(base[i], alias[i]) == 0;
    const int last_after = find_result(base[ALIASES - 1], alias[ALIASES - 1]);
    int closed = 1;
    for (int i = 0; i < ALIASES; i++)
        closed &= sceKernelCloseMemBlock(alias[i]) == 0;

    PRINT("allocate the original", test_created(original));
    PRINT("open every alias", open_ok);
    PRINT("open the 33rd alias", last_open);
    PRINT("the first 32 aliases: how many are found before the free", found_before);
    PRINT("the 33rd alias: find before the free", last_before);
    PRINT("free E", free_original);
    PRINT("the first 32 aliases: how many are found after the free", found_after);
    PRINT("the 33rd alias: find after the free", last_after);
    PRINT("close every alias", closed);
    test_exit();
    return 0;
}
