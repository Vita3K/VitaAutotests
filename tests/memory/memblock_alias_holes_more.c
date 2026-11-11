// What freeing an original unmaps when its aliases left holes in its alias list. An ENABLE_OPEN
// 64 KiB block E has five aliases A0 to A4 opened with 0x30, in that order. A0 and A1 are closed,
// which leaves three live aliases, and then E is freed. Each live alias is looked up by its base
// before and after the free, and the test prints the results, and the results of closing each
// alias after that. A find prints 0 when it gives the block, the error when it fails and 1 when
// it gives another UID.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define ALIASES 5
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
    const SceUID original = sceKernelAllocMemBlock("alias holes", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10000, (SceKernelAllocMemBlockOpt *)&opt);
    SceUID alias[ALIASES];
    void *base[ALIASES] = { NULL };
    int open_ok = 1;
    for (int i = 0; i < ALIASES; i++) {
        alias[i] = sceKernelOpenMemBlock("alias holes", 0x30);
        open_ok &= alias[i] >= 0;
        if (alias[i] >= 0)
            sceKernelGetMemBlockBase(alias[i], &base[i]);
    }
    const int close0 = sceKernelCloseMemBlock(alias[0]);
    const int close1 = sceKernelCloseMemBlock(alias[1]);
    int before[ALIASES], after[ALIASES], closed[ALIASES];
    for (int i = 2; i < ALIASES; i++)
        before[i] = find_result(base[i], alias[i]);
    const int free_original = sceKernelFreeMemBlock(original);
    for (int i = 2; i < ALIASES; i++)
        after[i] = find_result(base[i], alias[i]);
    for (int i = 2; i < ALIASES; i++)
        closed[i] = sceKernelCloseMemBlock(alias[i]);

    PRINT("allocate the original", test_created(original));
    PRINT("open the five aliases", open_ok);
    PRINT("close A0", close0);
    PRINT("close A1", close1);
    for (int i = 2; i < ALIASES; i++)
        PRINTF(before[i], "A%d: find before the free of E", i);
    PRINT("free E", free_original);
    for (int i = 2; i < ALIASES; i++)
        PRINTF(after[i], "A%d: find after the free of E", i);
    for (int i = 2; i < ALIASES; i++)
        PRINTF(closed[i], "A%d: close", i);
    test_exit();
    return 0;
}
