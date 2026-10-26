// sceKernelFindMemBlockByAddr and sceKernelGetMemBlockInfoByRange at the edges of aliases made
// with sceKernelOpenMemBlock. The first part makes 16 aliases of a 64 KiB ENABLE_OPEN USER_RW
// block, each opened after a 64 KiB spacer block that is freed once all of them are made, so an
// alias may have a page in no block right before it. It takes the first alias with such a page and
// prints the lookups at its first byte and at the bytes before it: one byte with sizes 0, 1 and 2,
// a page with a page, and a page with the whole alias. The test is skipped if no alias has such a
// page.
//
// The second part opens one alias of each kind, USER_RW and USER_RW_UNCACHE with flags 0x10 and
// 0x30, USER_MAIN_R with 0x10, and the four common dialog types with the flag each of them opens
// with, and prints the lookups at the first byte, the last byte, the middle and the whole alias of
// each. An alias that doesn't open prints its error and no lookups. The aliases are closed and
// their originals freed before the next one is made.
//
// A find prints 0 when it gives the alias, the error when it fails and 1 when it gives another UID.
// An info call prints its error, or 0 when it reports the alias's base and size, and 1 when it
// reports something else.
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

// The lookups for the alias with a page in no block before it, the first byte of it first
static const Range before_ranges[] = {
    { 0, 1 },
    { -1, 0 },
    { -1, 1 },
    { -1, 2 },
    { -4 * KIB, 4 * KIB },
    { -4 * KIB, 4 * KIB + SIZE },
};

// The lookups for every alias of the second part
static const Range edge_ranges[] = {
    { 0, 1 },
    { SIZE - 1, 1 },
    { SIZE / 2, 1 },
    { 0, SIZE },
};

// The aliases of the second part, each with the type of its original and the flags to open with
static const struct {
    const char *name;
    SceUInt32 type;
    SceUInt32 flags;
} kinds[] = {
    { "USER_RW 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x10 },
    { "USER_RW 0x30", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x30 },
    { "USER_RW_UNCACHE 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x10 },
    { "USER_RW_UNCACHE 0x30", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x30 },
    { "USER_MAIN_R 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 0x10 },
    { "USER_CDIALOG_R 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_R, 0x10 },
    { "USER_CDIALOG_RW 0x30", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, 0x30 },
    { "USER_CDIALOG_NC_R 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_R, 0x10 },
    { "USER_CDIALOG_NC_RW 0x30", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW, 0x30 },
};

static SceUID gap_error;

static int find_result(SceUID found, SceUID block) {
    return found == block ? 0 : found < 0 ? found : 1;
}

// Whether the page at addr is in no block, which the find gives the error of a freed spacer for
static int page_in_no_block(const char *addr) {
    return sceKernelFindMemBlockByAddr(addr, 0) == gap_error;
}

// Makes a 64 KiB USER_RW block of its own, and returns whether it has a base
static int make(Block *block, const char *name) {
    block->uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    void *base = NULL;
    const int made = block->uid >= 0 && sceKernelGetMemBlockBase(block->uid, &base) == 0 && base;
    block->base = base;
    return made;
}

// Opens an alias of the original with the flags, and returns whether it has a base
static int open_alias(Block *alias, const char *name, SceUInt32 flags) {
    alias->uid = sceKernelOpenMemBlock(name, flags);
    void *base = NULL;
    const int made = alias->uid >= 0 && sceKernelGetMemBlockBase(alias->uid, &base) == 0 && base;
    alias->base = base;
    return made;
}

// Prints the find and the info by range for each range, from the alias's base
static void check_ranges(const char *name, const Block *alias, const Range *list,
                         unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
        char *addr = alias->base + list[i].offset;
        const SceSize size = list[i].size;
        const char *sign = list[i].offset < 0 ? "-" : "+";
        const unsigned int offset = list[i].offset < 0 ? -list[i].offset : list[i].offset;
        PRINTF(find_result(sceKernelFindMemBlockByAddr(addr, size), alias->uid),
               "%s, %s0x%X, 0x%X bytes: find", name, sign, offset, (unsigned)size);
        SceKernelMemBlockInfo info = { .size = sizeof(info) };
        int result = sceKernelGetMemBlockInfoByRange(addr, size, &info);
        if (result == 0)
            result = info.mappedBase == alias->base && info.mappedSize == SIZE ? 0 : 1;
        PRINTF(result, "%s, %s0x%X, 0x%X bytes: info by range", name, sign, offset,
               (unsigned)size);
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

// Makes an openable block of the kind's type, opens one alias of it and prints its lookups
static void check_kind(const char *name, SceUInt32 type, SceUInt32 flags) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("alias edges", type, SIZE,
                                                   (SceKernelAllocMemBlockOpt *)&opt);
    PRINTF(test_created(original), "%s: allocate the openable block", name);
    if (original < 0)
        return;
    Block alias = { -1, NULL };
    const int made = open_alias(&alias, "alias edges", flags);
    PRINTF(test_created(alias.uid), "%s: open it", name);
    if (made)
        check_ranges(name, &alias, edge_ranges, COUNT(edge_ranges));
    cleanup(original, &alias, 1);
}

int main(void) {
    static Block aliases[CANDIDATES], spacers[CANDIDATES];
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID original = sceKernelAllocMemBlock("alias edges",
                                                   SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE,
                                                   (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the openable block", test_created(original));
    int made = original >= 0;
    for (int i = 0; i < CANDIDATES; i++) {
        made &= make(&spacers[i], "alias edges spacer");
        made &= open_alias(&aliases[i], "alias edges", 0x30);
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
        if (page_in_no_block(aliases[i].base - 4 * KIB))
            gap = &aliases[i];
    if (!gap) {
        cleanup(original, aliases, CANDIDATES);
        test_skip("no alias has a page in no block right before it");
        return 0;
    }
    // Printed once the test knows it runs, since a skip prints nothing but its own line
    PRINT("find the base of a freed spacer", gap_error);

    check_ranges("alias", gap, before_ranges, COUNT(before_ranges));
    cleanup(original, aliases, CANDIDATES);

    for (unsigned int i = 0; i < COUNT(kinds); i++)
        check_kind(kinds[i].name, kinds[i].type, kinds[i].flags);

    test_exit();
    return 0;
}
