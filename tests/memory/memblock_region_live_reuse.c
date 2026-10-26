// Where the blocks of each type are placed while others are live, and whether new blocks overlap
// the live ones. Model: memblock_types_all, whose region print gives the 256 MiB region of each
// type's base. Here the regions are 0x6 for the CDRAM type, 0x7 for the uncached type, and 0x8 for
// the cached main types, USER_MAIN_CDIALOG_RW among them. Four blocks are made and kept live at
// once: 64 KiB USER_RW, 256 KiB USER_CDRAM_RW, 64 KiB USER_RW_UNCACHE and 64 KiB
// USER_MAIN_CDIALOG_RW. The first and third are freed, new blocks of those two types are made in
// their place, and that is repeated once. The cases:
// - each block as it is made: its result, and whether its base is in its type's region
// - after each round: whether no two of the four live blocks have overlapping mapped ranges
// - last, the four live blocks are freed
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// The 256 MiB regions memblock_types_all prints for these types
#define REGION_CDRAM 0x6
#define REGION_UNCACHED 0x7
#define REGION_CACHED 0x8

static const struct {
    const char *name;
    SceUInt32 type;
    SceSize size;
    SceUIntPtr region;
} kinds[] = {
    {"USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB, REGION_CACHED},
    {"USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, REGION_CDRAM},
    {"USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * KIB, REGION_UNCACHED},
    {"USER_MAIN_CDIALOG_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_RW, 64 * KIB,
     REGION_CACHED},
};

// Whether the base of block is in the region
static int in_region(SceUID block, SceUIntPtr region) {
    void *base = NULL;
    if (sceKernelGetMemBlockBase(block, &base) < 0)
        return 0;
    return ((SceUIntPtr)base >> 28) == region;
}

// Fills first and end with the mapped range of block, and returns 0 when it has none
static int range_of(SceUID block, SceUIntPtr *first, SceUIntPtr *end) {
    void *base = NULL;
    if (sceKernelGetMemBlockBase(block, &base) < 0)
        return 0;
    SceKernelMemBlockInfo info = {.size = sizeof(info)};
    if (sceKernelGetMemBlockInfoByAddr(base, &info) < 0)
        return 0;
    *first = (SceUIntPtr)info.mappedBase;
    *end = *first + info.mappedSize;
    return 1;
}

// Whether no two of the count blocks have overlapping mapped ranges, and all of them have one
static int disjoint(const SceUID *blocks, unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
        SceUIntPtr a_first = 0, a_end = 0;
        if (!range_of(blocks[i], &a_first, &a_end))
            return 0;
        for (unsigned int j = i + 1; j < count; j++) {
            SceUIntPtr b_first = 0, b_end = 0;
            if (!range_of(blocks[j], &b_first, &b_end))
                return 0;
            if (a_first < b_end && b_first < a_end)
                return 0;
        }
    }
    return 1;
}

// Makes a block of kinds[kind] for round, printing its result and whether it's in its region
static SceUID make(const char *round, unsigned int kind) {
    const SceUID block = sceKernelAllocMemBlock("region", kinds[kind].type, kinds[kind].size, NULL);
    PRINTF(test_created(block), "%s: %s: make it", round, kinds[kind].name);
    if (block >= 0)
        PRINTF(in_region(block, kinds[kind].region), "%s: %s: in its type's region", round,
               kinds[kind].name);
    return block;
}

int main(void) {
    SceUID live[COUNT(kinds)];
    for (unsigned int i = 0; i < COUNT(kinds); i++)
        live[i] = make("live", i);
    PRINT("the live blocks don't overlap", disjoint(live, COUNT(kinds)));

    for (int round = 1; round <= 2; round++) {
        char label[16];
        sceClibSnprintf(label, sizeof(label), "round %d", round);
        PRINTF(sceKernelFreeMemBlock(live[0]), "%s: free the USER_RW block", label);
        PRINTF(sceKernelFreeMemBlock(live[2]), "%s: free the USER_RW_UNCACHE block", label);
        live[0] = make(label, 0);
        live[2] = make(label, 2);
        PRINTF(disjoint(live, COUNT(kinds)), "%s: no live block overlaps another", label);
    }

    for (unsigned int i = 0; i < COUNT(kinds); i++)
        PRINTF(sceKernelFreeMemBlock(live[i]), "free the %s block at the end", kinds[i].name);
    test_exit();
    return 0;
}
