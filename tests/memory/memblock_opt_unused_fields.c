// Option fields that the attr bits given don't use, with option size 0x14. Models:
// memblock_opt_attrs (the option sizes and attr bits) and memblock_base_block (a base block name,
// which only counts with HAS_BASE_NAME 0x200). A 64 KiB USER_RW block with ENABLE_OPEN is made
// first, named "opt base", with a word written in it. Then a 4 KiB USER_RW block is made with each
// of these options, and freed before the next:
// - attr 0 with the base block name "opt base" in the name field
// - attr 0 with the reserved field 0x12345678 and no name
// - HAS_ALIGNMENT (0x4) with alignment 0x1000 and the base block name "opt base"
// - HAS_ALIGNMENT with alignment 0x1000 and the reserved field 0x12345678
// For each block made, its result, then whether it's a plain block: USER_RW type, 4 KiB mapped, a
// 4 KiB aligned base, and outside the named block. Last, the named block still holds its word, and
// it opens with 0x10 and the opened UID holds the word too.
//
// The word is read only through a UID that the lookup by its base gives back.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define NAMED_SIZE (64 * KIB)
#define ENABLE_OPEN 0x4000
#define HAS_ALIGNMENT 0x4
#define NAME "opt base"
#define WORD 0x22222222

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static volatile SceUInt32 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Whether block holds the word, as found by the lookup by its base
static int holds_word(SceUID block) {
    if (block < 0)
        return 0;
    volatile SceUInt32 *words = base_of(block);
    return words && sceKernelFindMemBlockByAddr((void *)words, 0) == block && words[0] == WORD;
}

// Whether block is a plain 4 KiB USER_RW block at a 4 KiB aligned base, outside the named block
static int is_plain(SceUID block, SceUIntPtr named) {
    void *base = NULL;
    if (sceKernelGetMemBlockBase(block, &base) < 0)
        return 0;
    SceKernelMemBlockInfo info = {.size = sizeof(info)};
    if (sceKernelGetMemBlockInfoByAddr(base, &info) < 0)
        return 0;
    const SceUIntPtr at = (SceUIntPtr)base;
    const int outside = at + 4 * KIB <= named || at >= named + NAMED_SIZE;
    return info.mappedBase == base && info.mappedSize == 4 * KIB &&
           info.type == SCE_KERNEL_MEMBLOCK_TYPE_USER_RW && at % (4 * KIB) == 0 && outside;
}

// Makes a 4 KiB block with the option, frees it, and prints the result and whether it was plain
static void try_opt(const char *label, AllocOpt *opt, SceUIntPtr named) {
    const SceUID block = sceKernelAllocMemBlock("opt fields", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                4 * KIB, (SceKernelAllocMemBlockOpt *)opt);
    const int plain = block >= 0 && is_plain(block, named);
    if (block >= 0)
        sceKernelFreeMemBlock(block);
    PRINTF(test_created(block), "%s", label);
    if (block >= 0)
        PRINTF(plain, "%s: a plain block", label);
}

int main(void) {
    AllocOpt named_opt = {.size = sizeof(named_opt), .attr = ENABLE_OPEN};
    const SceUID named = sceKernelAllocMemBlock(NAME, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, NAMED_SIZE,
                                                (SceKernelAllocMemBlockOpt *)&named_opt);
    PRINT("the named block: make it", test_created(named));
    if (named >= 0)
        base_of(named)[0] = WORD;
    SceUIntPtr named_at = 0;
    if (named >= 0) {
        void *named_base = NULL;
        sceKernelGetMemBlockBase(named, &named_base);
        named_at = (SceUIntPtr)named_base;
    }

    AllocOpt name_only = {.size = sizeof(name_only), .baseBlockName = NAME};
    try_opt("attr 0 with the base block name", &name_only, named_at);
    AllocOpt reserved_only = {.size = sizeof(reserved_only), .reserved = 0x12345678};
    try_opt("attr 0 with the reserved field", &reserved_only, named_at);
    AllocOpt align_name = {.size = sizeof(align_name),
                           .attr = HAS_ALIGNMENT,
                           .alignment = 0x1000,
                           .baseBlockName = NAME};
    try_opt("HAS_ALIGNMENT with the base block name", &align_name, named_at);
    AllocOpt align_reserved = {.size = sizeof(align_reserved),
                               .attr = HAS_ALIGNMENT,
                               .alignment = 0x1000,
                               .reserved = 0x12345678};
    try_opt("HAS_ALIGNMENT with the reserved field", &align_reserved, named_at);

    PRINT("the named block still holds its word", holds_word(named));
    const SceUID opened = named >= 0 ? sceKernelOpenMemBlock(NAME, 0x10) : named;
    PRINT("open the named block with 0x10", test_created(opened));
    if (opened >= 0)
        PRINT("the opened block holds the word", holds_word(opened));
    if (opened >= 0)
        sceKernelCloseMemBlock(opened);
    if (named >= 0)
        sceKernelFreeMemBlock(named);
    test_exit();
    return 0;
}
