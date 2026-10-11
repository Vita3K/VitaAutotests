// Memory blocks: which types and sizes an allocation takes, how blocks are aligned, and what the
// lookup and info calls report. Addresses are printed as alignments and comparisons, never values.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

// The option struct games pass, without the fields vitasdk adds after strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

static const struct {
    const char *name;
    SceUInt32 type;
    SceSize unit;
} types[] = {
    { "USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB },
    { "USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB },
    { "USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB },
    { "USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB },
    { "USER_MAIN_PHYCONT_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, MIB },
};

// Returns 0 if the type takes size, or the error
static int alloc_result(SceUInt32 type, SceSize size) {
    const SceUID block = sceKernelAllocMemBlock("probe", type, size, NULL);
    if (block < 0)
        return block;
    sceKernelFreeMemBlock(block);
    return 0;
}

// Allocates one unit of a type and prints what the calls report about the block
static void check_type(const char *name, SceUInt32 type, SceSize unit) {
    const SceUID block = sceKernelAllocMemBlock(name, type, unit, NULL);
    PRINTF(test_created(block), "%s: allocate one unit", name);
    if (block < 0)
        return;
    void *base = NULL;
    PRINTF(sceKernelGetMemBlockBase(block, &base), "%s: get the base", name);
    PRINTF((SceUIntPtr)base % unit == 0, "%s: the base is aligned to the unit", name);

    volatile SceUInt32 *words = base;
    words[0] = 0x12345678;
    words[unit / 4 - 1] = 0x9ABCDEF0;
    PRINTF(words[0] == 0x12345678 && words[unit / 4 - 1] == 0x9ABCDEF0, "%s: the whole block holds writes", name);

    PRINTF(sceKernelFindMemBlockByAddr(base, 0) == block, "%s: find by the base", name);
    PRINTF(sceKernelFindMemBlockByAddr((char *)base + unit - 1, 1) == block, "%s: find by the last byte", name);
    PRINTF(sceKernelFindMemBlockByAddr(base, unit) == block, "%s: find by the whole range", name);

    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetMemBlockInfoByAddr(base, &info), "%s: info by address", name);
    PRINTF(info.mappedBase == base, "%s: its mapped base is the base", name);
    PRINTF(info.mappedSize, "%s: its mapped size", name);
    PRINTF(info.memoryType, "%s: its memory type", name);
    PRINTF(info.access, "%s: its access", name);
    PRINTF(info.type, "%s: its type", name);
    info.size = sizeof(info);
    PRINTF(sceKernelGetMemBlockInfoByRange(base, unit, &info), "%s: info by range", name);

    PRINTF(sceKernelFreeMemBlock(block), "%s: free", name);
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        check_type(types[i].name, types[i].type, types[i].unit);
        PRINTF(alloc_result(types[i].type, types[i].unit / 2), "%s: allocate half a unit", types[i].name);
        PRINTF(alloc_result(types[i].type, types[i].unit + 4 * KIB), "%s: allocate a unit and 4 KiB", types[i].name);
        PRINTF(alloc_result(types[i].type, 0), "%s: allocate 0 bytes", types[i].name);
    }

    // Types that aren't for games
    PRINT("allocate type 0", alloc_result(0, 4 * KIB));
    PRINT("allocate type 0xFFFFFFFF", alloc_result(0xFFFFFFFF, 4 * KIB));
    PRINT("allocate the read only main type", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 4 * KIB));
    PRINT("allocate the device type", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_DEVICE_RW, 4 * KIB));
    PRINT("allocate the game type", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_GAME_RW, 4 * KIB));
    PRINT("allocate the common dialog type", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_RW, 4 * KIB));
    PRINT("allocate the tool type", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_TOOL_RW, 4 * KIB));

    // Names
    PRINT("allocate with an empty name", test_created(sceKernelAllocMemBlock("", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL)));
    PRINT("allocate with a 31 character name", test_created(sceKernelAllocMemBlock("0123456789012345678901234567890", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL)));
    PRINT("allocate with a 32 character name", test_created(sceKernelAllocMemBlock("01234567890123456789012345678901", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL)));

    // The option struct
    AllocOpt opt = { .size = sizeof(opt), .attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT, .alignment = MIB };
    SceUID block = sceKernelAllocMemBlock("aligned", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate aligned to 1 MiB", test_created(block));
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    PRINT("the base is aligned to 1 MiB", (SceUIntPtr)base % MIB == 0);
    sceKernelFreeMemBlock(block);
    static const SceSize alignments[] = { 0, 2 * KIB, 4 * KIB, 12 * KIB, 16 * MIB, 32 * MIB };
    for (unsigned int i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
        opt.alignment = alignments[i];
        block = sceKernelAllocMemBlock("aligned", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt);
        PRINTF(test_created(block), "allocate aligned to 0x%X", alignments[i]);
        if (block >= 0)
            sceKernelFreeMemBlock(block);
    }
    opt.alignment = 4 * KIB;
    opt.size = 0;
    PRINT("allocate with an option size of 0", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt)));
    SceKernelAllocMemBlockOpt full = { .size = sizeof(full), .attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT, .alignment = 4 * KIB };
    PRINT("allocate with the vitasdk option size", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, &full)));
    opt.size = sizeof(opt);
    opt.attr = 0;
    PRINT("allocate with option attribute 0", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt)));
    opt.attr = 0x80000000;
    PRINT("allocate with option attribute 0x80000000", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, (SceKernelAllocMemBlockOpt *)&opt)));
    opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
    opt.alignment = 4 * KIB;
    PRINT("allocate CDRAM aligned to 4 KiB", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, (SceKernelAllocMemBlockOpt *)&opt)));
    PRINT("allocate physically contiguous memory with an alignment", test_created(sceKernelAllocMemBlock("o", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB, (SceKernelAllocMemBlockOpt *)&opt)));

    // Freeing and looking up blocks that aren't there
    block = sceKernelAllocMemBlock("freed", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
    sceKernelGetMemBlockBase(block, &base);
    PRINT("free", sceKernelFreeMemBlock(block));
    PRINT("free again", sceKernelFreeMemBlock(block));
    PRINT("get the base of a freed block", sceKernelGetMemBlockBase(block, &base));
    PRINT("find by the address of a freed block", sceKernelFindMemBlockByAddr(base, 0) == block);
    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    PRINT("free an unknown UID", sceKernelFreeMemBlock(0x12345));
    PRINT("free a semaphore", sceKernelFreeMemBlock(sema));
    PRINT("free UID 0", sceKernelFreeMemBlock(0));
    PRINT("get the base of a semaphore", sceKernelGetMemBlockBase(sema, &base));
    int local = 0;
    PRINT("find the block of the stack", test_created(sceKernelFindMemBlockByAddr(&local, 0)));
    PRINT("find the block of address 0", sceKernelFindMemBlockByAddr(NULL, 0));
    PRINT("find the block of address 4", sceKernelFindMemBlockByAddr((void *)4, 0));

    // Free memory
    SceKernelFreeMemorySizeInfo before = { .size = sizeof(before) }, after = { .size = sizeof(after) };
    PRINT("free memory size", sceKernelGetFreeMemorySize(&before));
    block = sceKernelAllocMemBlock("counted", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MIB, NULL);
    sceKernelGetFreeMemorySize(&after);
    PRINT("a 1 MiB block takes 1 MiB of user memory", before.size_user - after.size_user == MIB);
    PRINT("and no CDRAM", before.size_cdram == after.size_cdram);
    sceKernelFreeMemBlock(block);
    sceKernelGetFreeMemorySize(&after);
    PRINT("freeing it gives it back", before.size_user == after.size_user);
    after.size = 0;
    PRINT("free memory size with a size of 0", sceKernelGetFreeMemorySize(&after));

    // Blocks for code
    block = sceKernelAllocMemBlockForVM("vm", MIB);
    PRINT("allocate for code", test_created(block));
    if (block >= 0)
        PRINT("free it", sceKernelFreeMemBlock(block));
    PRINT("allocate for code with 4 KiB", test_created(sceKernelAllocMemBlockForVM("vm", 4 * KIB)));

    // A huge size and NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("allocate 1 GiB", alloc_result(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 1024 * MIB));
    PRINT("get the base into a NULL pointer", sceKernelGetMemBlockBase(sceKernelAllocMemBlock("n", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL), NULL));
    PRINT("info into a NULL pointer", sceKernelGetMemBlockInfoByAddr(&local, NULL));
    PRINT("free memory size into a NULL pointer", sceKernelGetFreeMemorySize(NULL));
    PRINT("allocate with a NULL name", test_created(sceKernelAllocMemBlock(NULL, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL)));

    test_exit();
    return 0;
}
