// Failing opens by name, and whether a failed open takes or leaves memory, then a good open of a
// USER_RW name after them. Three originals are made with ENABLE_OPEN: a 64 KiB USER_RW block
// holding a word of its own, a 256 KiB USER_CDRAM_RW block, and a 64 KiB USER_RW_UNCACHE block.
// The cases:
// - the CDRAM name opened with 0x10, then with 0x30
// - the USER_RW name opened with flags 0
// - a name that no block holds, opened with 0x10
// - the USER_RW_UNCACHE name opened with 0x10, after which that original is freed
// Each open prints its result, and whether the main, CDRAM and physically contiguous free sizes
// are unchanged around it. Last, the USER_RW name is opened with 0x30, which must map the word of
// its original, read through the opened block, before that block is closed. memblock_open covers
// the flag bits and the CDRAM name with 0x30 on their own, this test runs the failing names one
// after another with the free sizes measured around each.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ENABLE_OPEN 0x4000
#define WORD 0x5A5A1234
#define PROBES 320

#define NAME_USER "open fail user"
#define NAME_CDRAM "open fail cdram"
#define NAME_UNCACHE "open fail uncache"
#define NAME_MISSING "open fail missing"

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

// Makes an original with ENABLE_OPEN of the type and size
static SceUID make_original(const char *name, SceUInt32 type, SceSize size) {
    AllocOpt opt = { .size = sizeof(opt), .attr = ENABLE_OPEN };
    return sceKernelAllocMemBlock(name, type, size, (SceKernelAllocMemBlockOpt *)&opt);
}

// Opens the name with the flags, and prints its result and whether the free
// sizes are unchanged
static void failed_open(const char *label, const char *name, int flags) {
    const Sizes before = measure_all();
    const SceUID opened = sceKernelOpenMemBlock(name, flags);
    const Sizes after = measure_all();
    PRINTF(test_created(opened), "%s: open it", label);
    PRINTF(same_sizes(&before, &after), "%s: the free sizes are unchanged", label);
    if (opened >= 0)
        sceKernelCloseMemBlock(opened);
}

int main(void) {
    // The first measures only warm up
    measure_all();

    const SceUID user = make_original(NAME_USER, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB);
    PRINT("make the USER_RW original", test_created(user));
    if (user >= 0)
        base_of(user)[0] = WORD;
    const SceUID cdram =
        make_original(NAME_CDRAM, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB);
    PRINT("make the CDRAM original", test_created(cdram));

    failed_open("the CDRAM name with 0x10", NAME_CDRAM, 0x10);
    failed_open("the CDRAM name with 0x30", NAME_CDRAM, 0x30);
    failed_open("the USER_RW name with flags 0", NAME_USER, 0);
    failed_open("a name no block holds, with 0x10", NAME_MISSING, 0x10);

    const SceUID uncache =
        make_original(NAME_UNCACHE, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * KIB);
    PRINT("make the USER_RW_UNCACHE original", test_created(uncache));
    failed_open("the USER_RW_UNCACHE name with 0x10", NAME_UNCACHE, 0x10);
    if (uncache >= 0)
        PRINT("free the USER_RW_UNCACHE original", sceKernelFreeMemBlock(uncache));

    const SceUID good = sceKernelOpenMemBlock(NAME_USER, 0x30);
    PRINT("open the USER_RW name with 0x30 after them", test_created(good));
    if (good >= 0) {
        PRINT("the opened block maps the word", base_of(good)[0] == WORD);
        PRINT("close it", sceKernelCloseMemBlock(good));
    }

    if (cdram >= 0)
        PRINT("free the CDRAM original", sceKernelFreeMemBlock(cdram));
    if (user >= 0)
        PRINT("free the USER_RW original", sceKernelFreeMemBlock(user));
    test_exit();
    return 0;
}
