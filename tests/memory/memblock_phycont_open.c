// sceKernelOpenMemBlock on a PHYCONT block with ENABLE_OPEN: a 1 MiB USER_RW block with the PHYCONT
// attribute (0x200000) and ENABLE_OPEN (0x4000), opened with flags 0x10 and then 0x30. The cases:
// - the original: how many KiB it takes from the main, CDRAM and physically contiguous free sizes
// - each open: its result, and whether the free sizes are unchanged by it
// - each alias: whether its type, memory type, access and mapped size are the original's, and
//   whether it reads the original's word
// - a write through the original, read through the 0x30 alias
// Then the original is freed, and whether the free sizes are back to what they were before it. The
// aliases are closed before the original is freed. The free sizes are read into variables during
// the test and printed at the end, so the output the test writes doesn't take memory in between the
// reads.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define ENABLE_OPEN 0x4000
#define ORIGINAL_WORD 0x13579BDF
#define WRITTEN_WORD 0x2468ACE0

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

typedef struct Alias {
    int flags;
    int result;
    int same_sizes;
    int type;
    int memory_type;
    int access;
    int size;
    int reads_original;
    int reads_written;
} Alias;

static SceKernelFreeMemorySizeInfo free_sizes(void) {
    SceKernelFreeMemorySizeInfo info = { .size = sizeof(info) };
    sceKernelGetFreeMemorySize(&info);
    return info;
}

static int same_sizes(const SceKernelFreeMemorySizeInfo *a, const SceKernelFreeMemorySizeInfo *b) {
    return a->size_user == b->size_user && a->size_cdram == b->size_cdram &&
           a->size_phycont == b->size_phycont;
}

static volatile SceUInt32 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Opens the original's name with flags, records what it gives and what the alias reads, and closes
// it
static void open_alias(Alias *alias, const char *name, volatile SceUInt32 *mine,
                       const SceKernelMemBlockInfo *original) {
    const SceKernelFreeMemorySizeInfo before = free_sizes();
    const SceUID opened = sceKernelOpenMemBlock(name, alias->flags);
    const SceKernelFreeMemorySizeInfo after = free_sizes();
    alias->result = test_created(opened);
    alias->same_sizes = same_sizes(&before, &after);
    if (opened < 0)
        return;
    volatile SceUInt32 *theirs = base_of(opened);
    SceKernelMemBlockInfo info = { .size = sizeof(info) };
    sceKernelGetMemBlockInfoByAddr((void *)theirs, &info);
    alias->type = info.type == original->type;
    alias->memory_type = info.memoryType == original->memoryType;
    alias->access = info.access == original->access;
    alias->size = info.mappedSize == original->mappedSize;
    alias->reads_original = theirs[0] == ORIGINAL_WORD;
    if (alias->flags == 0x30) {
        mine[0] = WRITTEN_WORD;
        alias->reads_written = theirs[0] == WRITTEN_WORD;
    }
    sceKernelCloseMemBlock(opened);
}

static void print_alias(const char *label, const Alias *alias) {
    PRINTF(alias->result, "%s: the result", label);
    PRINTF(alias->same_sizes, "%s: the free sizes are unchanged", label);
    if (alias->result < 0)
        return;
    PRINTF(alias->type, "%s: its type is the original's", label);
    PRINTF(alias->memory_type, "%s: its memory type is the original's", label);
    PRINTF(alias->access, "%s: its access is the original's", label);
    PRINTF(alias->size, "%s: its mapped size is the original's", label);
    PRINTF(alias->reads_original, "%s: it reads the original's word", label);
    if (alias->flags == 0x30)
        PRINTF(alias->reads_written, "%s: it reads a write through the original", label);
}

int main(void) {
    const SceKernelFreeMemorySizeInfo before = free_sizes();
    AllocOpt opt = { .size = sizeof(opt),
                     .attr = ENABLE_OPEN | SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT };
    const SceUID original = sceKernelAllocMemBlock("phycont open", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                   MIB, (SceKernelAllocMemBlockOpt *)&opt);
    PRINT("allocate the PHYCONT block", test_created(original));
    if (original < 0) {
        test_exit();
        return 0;
    }
    const SceKernelFreeMemorySizeInfo during = free_sizes();
    const int taken_main = (before.size_user - during.size_user) / KIB;
    const int taken_cdram = (before.size_cdram - during.size_cdram) / KIB;
    const int taken_phycont = (before.size_phycont - during.size_phycont) / KIB;

    volatile SceUInt32 *mine = base_of(original);
    mine[0] = ORIGINAL_WORD;
    SceKernelMemBlockInfo original_info = { .size = sizeof(original_info) };
    sceKernelGetMemBlockInfoByAddr((void *)mine, &original_info);

    Alias read_only = { .flags = 0x10 };
    open_alias(&read_only, "phycont open", mine, &original_info);
    Alias read_write = { .flags = 0x30 };
    open_alias(&read_write, "phycont open", mine, &original_info);

    sceKernelFreeMemBlock(original);
    const SceKernelFreeMemorySizeInfo after = free_sizes();

    PRINT("the original: KiB taken from main", taken_main);
    PRINT("the original: KiB taken from CDRAM", taken_cdram);
    PRINT("the original: KiB taken from physically contiguous memory", taken_phycont);
    print_alias("open with 0x10", &read_only);
    print_alias("open with 0x30", &read_write);
    PRINT("the free sizes are back after the free", same_sizes(&before, &after));

    test_exit();
    return 0;
}
