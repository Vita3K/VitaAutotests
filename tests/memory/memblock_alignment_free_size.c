// How much free memory an aligned block takes, next to the same block made
// without alignment. Each of three kinds is made once without alignment and
// once with HAS_ALIGNMENT and an alignment of 0x1000000 (16 MiB), and ended.
// The kinds:
// - USER_RW of 4 KiB
// - USER_RW_UNCACHE of 4 KiB
// - USER_CDRAM_RW of 256 KiB
// For each aligned block, whether main memory, CDRAM and physically contiguous
// memory each drop by what the unaligned block drops by, and whether each drops
// by the size asked. Then whether the free sizes are back once each block ends.
// The free sizes are measured the way app_free_sizes does, main memory and
// CDRAM to the page and physically contiguous memory as reported. Everything is
// measured right before and after each call, so the output doesn't take memory
// in between. memblock_kind_sizes covers the same kinds at an alignment of 1
// MiB, this test uses 16 MiB.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define ALIGNMENT 0x1000000
#define PROBES 320
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

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

// One make and end of a block, with the free sizes around the make and around
// the end
typedef struct Taken {
    SceUID block;
    int freed;
    Sizes before, with, after;
} Taken;

static const struct {
    const char *name;
    SceUInt32 type;
    SceSize size;
} kinds[] = {
    { "USER_RW, 4 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB },
    { "USER_RW_UNCACHE, 4 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * KIB },
    { "USER_CDRAM_RW, 256 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB },
};

static Taken plain[COUNT(kinds)], aligned[COUNT(kinds)];
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

// The bytes a make took from main memory, CDRAM and physically contiguous
// memory
static Sizes taken_by(const Taken *taken) {
    const Sizes sizes = {
        taken->before.main - taken->with.main,
        taken->before.cdram - taken->with.cdram,
        taken->before.phycont - taken->with.phycont,
    };
    return sizes;
}

// Makes a block of the kind, aligned if asked, and ends it, keeping the free
// sizes around both
static void take(unsigned int i, int aligned_make, Taken *taken) {
    AllocOpt opt = {
        .size = sizeof(opt),
        .attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT,
        .alignment = ALIGNMENT,
    };
    taken->before = measure_all();
    taken->block = sceKernelAllocMemBlock("alignment free", kinds[i].type, kinds[i].size,
                                          aligned_make ? (SceKernelAllocMemBlockOpt *)&opt : NULL);
    taken->with = measure_all();
    taken->freed = taken->block >= 0 ? sceKernelFreeMemBlock(taken->block) : 0;
    taken->after = measure_all();
}

// Prints whether the aligned block takes what the unaligned one takes, and the
// size asked
static void print_against(const char *name, const Taken *unaligned, const Taken *aligned_taken,
                          SceSize size) {
    const Sizes plain_took = taken_by(unaligned), aligned_took = taken_by(aligned_taken);
    PRINTF(aligned_took.main == plain_took.main,
           "%s, aligned: main memory takes what the unaligned one does", name);
    PRINTF(aligned_took.main == (int)size, "%s, aligned: main memory takes the size asked", name);
    PRINTF(aligned_took.cdram == plain_took.cdram,
           "%s, aligned: CDRAM takes what the unaligned one does", name);
    PRINTF(aligned_took.cdram == (int)size, "%s, aligned: CDRAM takes the size asked", name);
    PRINTF(aligned_took.phycont == plain_took.phycont,
           "%s, aligned: physically contiguous memory takes what the unaligned "
           "one does",
           name);
    PRINTF(aligned_took.phycont == (int)size,
           "%s, aligned: physically contiguous memory takes the size asked", name);
}

static void print_kind(unsigned int i) {
    const char *name = kinds[i].name;
    const Taken *unaligned = &plain[i], *aligned_taken = &aligned[i];

    PRINTF(test_created(unaligned->block), "%s: make it", name);
    if (unaligned->block >= 0)
        PRINTF(unaligned->freed, "%s: free it", name);
    PRINTF(same_sizes(&unaligned->before, &unaligned->after),
           "%s: the free sizes are back once it ends", name);

    PRINTF(test_created(aligned_taken->block), "%s, aligned: make it", name);
    if (aligned_taken->block >= 0) {
        print_against(name, unaligned, aligned_taken, kinds[i].size);
        PRINTF(aligned_taken->freed, "%s, aligned: free it", name);
    }
    PRINTF(same_sizes(&aligned_taken->before, &aligned_taken->after),
           "%s, aligned: the free sizes are back once it ends", name);
}

int main(void) {
    // The first measures only warm up
    measure(MAIN);
    measure(CDRAM);
    for (unsigned int i = 0; i < COUNT(kinds); i++) {
        take(i, 0, &plain[i]);
        take(i, 1, &aligned[i]);
    }
    for (unsigned int i = 0; i < COUNT(kinds); i++)
        print_kind(i);

    test_exit();
    return 0;
}
