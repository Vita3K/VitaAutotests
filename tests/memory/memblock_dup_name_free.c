// Duplicate names of ENABLE_OPEN blocks, and a name that matches a live one in
// its first 31 characters. The cases:
// - a 64 KiB USER_RW block named "dup" with ENABLE_OPEN stays live, holding a word of its own
// - a second 64 KiB USER_RW block with ENABLE_OPEN and the same name
// - a 256 KiB USER_CDRAM_RW block with ENABLE_OPEN and the same name
// - a 64 KiB USER_RW block with ENABLE_OPEN named by 31 characters, "dup" and 28 more, made
//   live before the next case
// - a 64 KiB USER_RW block with ENABLE_OPEN named by 32 characters, whose first 31 are that name
// - opening "dup" with 0x10, and whether it maps the first block by the word in it
// The main, CDRAM and physically contiguous free sizes are measured right before and after each
// refused block, the way app_free_sizes does for main memory and CDRAM, and physically contiguous
// memory as reported. Every block that is made is freed at the end. memblock_open_names covers
// two ENABLE_OPEN USER_RW blocks with one name, this test adds a CDRAM block and the names that
// differ only after their 31st character.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000
#define FIRST_WORD 0x11111111
#define SECOND_WORD 0x22222222
#define PROBES 320

#define NAME "dup"
#define NAME_31 "dup-abcdefghijklmnopqrstuvwxyz0"
#define NAME_32 NAME_31 "1"

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

enum Kind { MAIN, CDRAM };

typedef struct Sizes {
    int main;
    int cdram;
    int phycont;
} Sizes;

static SceUID probes[PROBES];

static SceKernelFreeMemorySizeInfo reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes;
}

static int reported_kind(enum Kind kind) {
    const SceKernelFreeMemorySizeInfo sizes = reported();
    return kind == MAIN ? sizes.size_user : sizes.size_cdram;
}

// The free size of the kind to the unit of its probe blocks, or -1 if it never
// drops
static int measure(enum Kind kind) {
    const SceUInt32 type =
        kind == MAIN ? SCE_KERNEL_MEMBLOCK_TYPE_USER_RW : SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    const SceSize unit = kind == MAIN ? 4 * KIB : 256 * KIB;
    const int start = reported_kind(kind);
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", type, unit, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported_kind(kind);
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * (int)unit;
}

static Sizes measure_all(void) {
    const Sizes sizes = { measure(MAIN), measure(CDRAM), reported().size_phycont };
    return sizes;
}

static int same_sizes(const Sizes *a, const Sizes *b) {
    return a->main == b->main && a->cdram == b->cdram && a->phycont == b->phycont;
}

static volatile SceUInt32 *base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return base;
}

// Makes a 64 KiB USER_RW block with ENABLE_OPEN, and writes word at its start
static SceUID make(const char *name, SceUInt32 word) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const SceUID block = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB,
                                                (SceKernelAllocMemBlockOpt *)&opt);
    if (block >= 0)
        base_of(block)[0] = word;
    return block;
}

// Tries a block with ENABLE_OPEN of the type and size, which the name refuses,
// and prints its result and whether the free sizes are unchanged around the try
static void refused(const char *label, const char *name, SceUInt32 type, SceSize size) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    const Sizes before = measure_all();
    const SceUID block =
        sceKernelAllocMemBlock(name, type, size, (SceKernelAllocMemBlockOpt *)&opt);
    const Sizes after = measure_all();
    PRINTF(test_created(block), "%s: make it", label);
    PRINTF(same_sizes(&before, &after), "%s: the free sizes are unchanged", label);
    if (block >= 0)
        sceKernelFreeMemBlock(block);
}

// Opens the name with 0x10, and prints whether it maps the first block by the
// word in it
static void open_first(void) {
    const SceUID opened = sceKernelOpenMemBlock(NAME, 0x10);
    PRINT("open dup with 0x10", test_created(opened));
    if (opened < 0)
        return;
    volatile SceUInt32 *theirs = base_of(opened);
    PRINT("open dup maps the first block", theirs[0] == FIRST_WORD);
    PRINT("close it", sceKernelCloseMemBlock(opened));
}

int main(void) {
    // The first measures only warm up
    measure_all();

    const SceUID first = make(NAME, FIRST_WORD);
    PRINT("a 64 KiB USER_RW block named dup: make it", test_created(first));

    refused("a second 64 KiB USER_RW block named dup", NAME, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
            64 * KIB);
    refused("a 256 KiB USER_CDRAM_RW block named dup", NAME, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
            256 * KIB);

    const SceUID named_31 = make(NAME_31, SECOND_WORD);
    PRINT("a 64 KiB USER_RW block named by 31 characters: make it", test_created(named_31));
    refused("a 64 KiB USER_RW block named by 32 characters", NAME_32,
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB);

    open_first();

    if (named_31 >= 0)
        PRINT("free the 31 character block", sceKernelFreeMemBlock(named_31));
    if (first >= 0)
        PRINT("free the dup block", sceKernelFreeMemBlock(first));
    test_exit();
    return 0;
}
