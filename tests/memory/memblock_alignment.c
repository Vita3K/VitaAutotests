// Which alignments sceKernelAllocMemBlock takes with HAS_ALIGNMENT, per type, and whether a block
// it makes has its base aligned that much. Each case asks for one unit of the type: 4 KiB of
// USER_RW and USER_RW_UNCACHE, 256 KiB of USER_CDRAM_RW and 1 MiB of USER_MAIN_PHYCONT_RW. The
// alignments are every power of two from 1 to 2 GiB for USER_RW, the powers from 1 to 64 MiB for the
// others, and a few that aren't powers of two. Then 70 MiB of USER_CDRAM_RW with an alignment of 16,
// the block Disgaea 3 makes. The base is only checked for alignments up to 16 MiB, the most the types
// document, since a block may happen to be aligned more than it was asked. Last, an alignment of 0x3000, which HAS_ALIGNMENT
// refuses, without HAS_ALIGNMENT.
//
// On a PS Vita TV on firmware 3.65 USER_RW takes every power of two up to 16 MiB and
// USER_RW_UNCACHE every one from 4 KiB to 16 MiB, and the base is aligned to it. Larger alignments,
// and ones that aren't powers of two, fail with 0x80020005 (SCE_KERNEL_ERROR_INVALID_ARGUMENT).
// USER_CDRAM_RW takes 256 KiB to 64 MiB and aligns the base to it up to 16 MiB, and from 4 KiB to
// 128 KiB fails with 0x80024309 (SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE_CDRAM). Under 4 KiB,
// USER_RW_UNCACHE, USER_CDRAM_RW and USER_MAIN_PHYCONT_RW take every power of two, so the
// 70 MiB block with 16 works. USER_MAIN_PHYCONT_RW takes none from 4 KiB up.
// The alignment field is ignored without HAS_ALIGNMENT.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

typedef struct Opt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUID baseBlock;
    const char *baseBlockName;
} Opt;

// Prints the result of an allocation with the alignment, and whether the base is aligned to it
static void try_alignment(const char *name, SceUInt32 type, SceSize unit, SceUInt32 attr, SceSize alignment) {
    Opt opt = { .size = sizeof(opt), .attr = attr, .alignment = alignment };
    const SceUID block = sceKernelAllocMemBlock("aligned", type, unit, (SceKernelAllocMemBlockOpt *)&opt);
    void *base = NULL;
    // A base the lookup didn't give would pass any alignment
    int got_base = 0;
    if (block >= 0) {
        got_base = sceKernelGetMemBlockBase(block, &base) == 0 && base != NULL;
        sceKernelFreeMemBlock(block);
    }
    const char *how = attr ? "" : " without HAS_ALIGNMENT";
    PRINTF(test_created(block), "%s aligned to 0x%X%s", name, alignment, how);
    if (attr && block >= 0 && alignment != 0 && alignment <= 16 * MIB)
        PRINTF(got_base && (SceUIntPtr)base % alignment == 0, "%s aligned to 0x%X%s: the base is aligned", name, alignment, how);
}

static void powers(const char *name, SceUInt32 type, SceSize unit, int from_bit, int to_bit) {
    for (int bit = from_bit; bit <= to_bit; bit++)
        try_alignment(name, type, unit, SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT, 1u << bit);
}

int main(void) {
    const SceUInt32 has = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
    powers("USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, 0, 31);
    static const SceSize odd[] = { 0x1800, 0x3000, 0x5000, 0x10001, 0xFFFFFFFF };
    for (unsigned int i = 0; i < sizeof(odd) / sizeof(odd[0]); i++)
        try_alignment("USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, has, odd[i]);

    powers("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB, 0, 11);
    powers("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB, 12, 26);
    try_alignment("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB, has, 0x3000);

    powers("USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, 0, 11);
    powers("USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, 12, 26);
    try_alignment("USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, has, 0x60000);
    // The block Disgaea 3 makes, which a Vita takes with an alignment of 16 bytes
    try_alignment("USER_CDRAM_RW 70 MiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 70 * MIB, has, 0x10);

    powers("USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB, 0, 11);
    powers("USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB, 12, 21);

    try_alignment("USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, 0, 0x3000);
    test_exit();
    return 0;
}
