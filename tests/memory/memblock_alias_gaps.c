// sceKernelFindMemBlockByAddr and sceKernelGetMemBlockInfoByRange for ranges around the end of a 64
// KiB alias made with sceKernelOpenMemBlock and flags 0x30. An ENABLE_OPEN 64 KiB block is made,
// and 16 aliases are opened from it, each followed by a 64 KiB spacer block that is freed after.
// The test then looks for an alias with 64 KiB in no block right after it, as memblock_find_gaps
// does, and ends with test_skip() if there is none. A page is in no block when the find gives it
// the error the base of a freed spacer gives, which the test prints first. The cases, as offsets
// from the alias's base:
// - the whole range, found by the alias and its info found by range
// - its last byte with sizes 0, 1 and 2
// - one byte past its end with sizes 0 and 1, and a page past its end
// - its whole range plus one byte
// Then the info call by address at its last byte and at one byte past its end. The aliases are
// closed, and the original freed, after the output is printed. A find prints 0 when it gives the
// alias, the error when it fails and 1 when it gives another UID. An info call prints its error, or
// 0 when it reports the alias's base and size, and 1 when it reports something else.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)
#define CANDIDATES 16
#define ENABLE_OPEN 0x4000
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

typedef struct Block {
    SceUID uid;
    char *base;
} Block;

typedef struct Range {
    int offset;
    SceSize size;
} Range;

static const Range ranges[] = {
    { 0, SIZE }, { SIZE - 1, 0 }, { SIZE - 1, 1 },   { SIZE - 1, 2 },
    { SIZE, 0 }, { SIZE, 1 },     { SIZE, 4 * KIB }, { 0, SIZE + 1 },
};

static SceUID gap_error;

static int find_result(SceUID found, SceUID block) {
    return found == block ? 0 : found < 0 ? found : 1;
}

// Whether the 64 KiB from addr are in no block, a page at a time
static int in_no_block(const char *addr) {
    for (SceSize offset = 0; offset < SIZE; offset += 4 * KIB)
        if (sceKernelFindMemBlockByAddr(addr + offset, 0) != gap_error)
            return 0;
    return 1;
}

// Makes a 64 KiB USER_RW block of its own, and returns whether it has a base
static int make(Block *block, const char *name) {
    block->uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    void *base = NULL;
    const int made = block->uid >= 0 && sceKernelGetMemBlockBase(block->uid, &base) == 0 && base;
    block->base = base;
    return made;
}

// Opens an alias of the original with flags 0x30, and returns whether it has a base
static int open_alias(Block *alias, const char *name) {
    alias->uid = sceKernelOpenMemBlock(name, 0x30);
    void *base = NULL;
    const int made = alias->uid >= 0 && sceKernelGetMemBlockBase(alias->uid, &base) == 0 && base;
    alias->base = base;
    return made;
}

static void check_ranges(const char *name, const Block *alias) {
    for (unsigned int i = 0; i < COUNT(ranges); i++) {
        char *addr = alias->base + ranges[i].offset;
        const SceSize size = ranges[i].size;
        const char *sign = ranges[i].offset < 0 ? "-" : "+";
        const unsigned int offset = ranges[i].offset < 0 ? -ranges[i].offset : ranges[i].offset;
        PRINTF(find_result(sceKernelFindMemBlockByAddr(addr, size), alias->uid),
               "%s, %s0x%X, 0x%X bytes: find", name, sign, offset, (unsigned)size);
        SceKernelMemBlockInfo info = { .size = sizeof(info) };
        int result = sceKernelGetMemBlockInfoByRange(addr, size, &info);
        if (result == 0)
            result = info.mappedBase == alias->base && info.mappedSize == SIZE ? 0 : 1;
        PRINTF(result, "%s, %s0x%X, 0x%X bytes: info by range", name, sign, offset, (unsigned)size);
    }
}

// Closes the aliases that were opened, then frees the original
static void cleanup(SceUID original, const Block *aliases, int count) {
    for (int i = 0; i < count; i++)
        if (aliases[i].uid >= 0)
            sceKernelCloseMemBlock(aliases[i].uid);
    if (original >= 0)
        sceKernelFreeMemBlock(original);
}

int main(void) {
    static Block aliases[CANDIDATES], spacers[CANDIDATES];
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("alias gaps", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                   SIZE, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the openable block", test_created(original));
    int made = original >= 0;
    for (int i = 0; i < CANDIDATES; i++) {
        made &= open_alias(&aliases[i], "alias gaps");
        made &= make(&spacers[i], "alias gaps spacer");
    }
    // Nothing is made from here on, so the spacers' addresses stay in no block
    for (int i = 0; i < CANDIDATES; i++) {
        if (spacers[i].uid >= 0)
            sceKernelFreeMemBlock(spacers[i].uid);
        spacers[i].uid = -1;
    }
    if (!made) {
        PRINT("the aliases are made", 0);
        cleanup(original, aliases, CANDIDATES);
        test_exit();
        return 0;
    }
    gap_error = sceKernelFindMemBlockByAddr(spacers[0].base, 0);
    if (gap_error >= 0) {
        PRINT("find the base of a freed spacer", gap_error);
        cleanup(original, aliases, CANDIDATES);
        test_exit();
        return 0;
    }

    const Block *gap = NULL;
    for (int i = 0; i < CANDIDATES && !gap; i++)
        if (in_no_block(aliases[i].base + SIZE))
            gap = &aliases[i];
    if (!gap) {
        cleanup(original, aliases, CANDIDATES);
        test_skip("no alias has 64 KiB in no block after it");
        return 0;
    }
    // Printed once the test knows it runs, since a skip prints nothing but its own line
    PRINT("find the base of a freed spacer", gap_error);

    check_ranges("alias", gap);
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetMemBlockInfoByAddr(gap->base + SIZE - 1, &info),
           "alias, +0x%X: info by address", SIZE - 1);
    PRINTF(sceKernelGetMemBlockInfoByAddr(gap->base + SIZE, &info), "alias, +0x%X: info by address",
           SIZE);

    cleanup(original, aliases, CANDIDATES);
    test_exit();
    return 0;
}
