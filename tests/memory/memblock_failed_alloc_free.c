// A failing sceKernelAllocMemBlock takes nothing from the free sizes and leaves no block behind.
// Each case asks for a block the call may refuse, prints the result, and then whether the free
// sizes are the same as before the call. Main memory and CDRAM are measured to the page the way
// app_free_sizes does, and physically contiguous memory is read as reported. A block that a case
// does get is freed right away, so its result shows as a success and its free sizes come back. The
// cases:
// - USER_RW of 0x800 bytes, under its 4 KiB unit
// - USER_CDRAM_RW of 0x20000 bytes, under its 256 KiB unit
// - USER_MAIN_PHYCONT_RW of 0x80000 bytes, under its 1 MiB unit
// - USER_RW with HAS_ALIGNMENT and an alignment of 0x3000, which is not a power of two
// - USER_RW with an option struct whose size is 0x10
// - the RX type 0x0C20D050 of 4 KiB, which an app may not allocate
// - USER_RW of 1 GiB, last, since a wrong implementation may make a block of that size
// Everything is measured first and printed after, so the output file the test writes does not take
// memory in between the measures.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define PROBES 320

enum Kind { MAIN, CDRAM };

typedef struct Sizes {
    int main;
    int cdram;
    int phycont;
} Sizes;

// The option struct games pass, which ends at strBaseBlockName
typedef struct AllocOpt {
    SceSize size;
    SceUInt32 attr;
    SceSize alignment;
    SceUInt32 reserved;
    const char *baseBlockName;
} AllocOpt;

typedef struct Case {
    const char *name;
    SceUInt32 type;
    SceSize size;
    SceKernelAllocMemBlockOpt *opt;
    int result;
    int unchanged;
} Case;

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

// The free size of the kind to the unit of its probe blocks, or -1 if it never drops
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

// Allocates the case's block, frees it if one was made, and records the result and the free sizes
static void run_case(Case *c) {
    const Sizes before = measure_all();
    const SceUID block = sceKernelAllocMemBlock("failed", c->type, c->size, c->opt);
    c->result = test_created(block);
    if (block >= 0)
        sceKernelFreeMemBlock(block);
    const Sizes after = measure_all();
    c->unchanged = same_sizes(&before, &after);
}

int main(void) {
    // Warms up the measures, whose first call isn't printed
    measure(MAIN);
    measure(CDRAM);

    AllocOpt tiny = { .size = 0x10 };
    AllocOpt aligned = {
        .size = sizeof(aligned),
        .attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT,
        .alignment = 0x3000,
    };
    Case cases[] = {
        { "USER_RW, 0x800 bytes", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x800, NULL, 0, 0 },
        { "USER_CDRAM_RW, 0x20000 bytes", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x20000, NULL, 0,
          0 },
        { "USER_MAIN_PHYCONT_RW, 0x80000 bytes", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW,
          0x80000, NULL, 0, 0 },
        { "USER_RW, alignment 0x3000", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB,
          (SceKernelAllocMemBlockOpt *)&aligned, 0, 0 },
        { "USER_RW, option size 0x10", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB,
          (SceKernelAllocMemBlockOpt *)&tiny, 0, 0 },
        { "RX type 0x0C20D050, 4 KiB", 0x0C20D050, 4 * KIB, NULL, 0, 0 },
        { "USER_RW, 1 GiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 1024 * MIB, NULL, 0, 0 },
    };
    const unsigned int count = sizeof(cases) / sizeof(cases[0]);

    for (unsigned int i = 0; i < count; i++)
        run_case(&cases[i]);

    for (unsigned int i = 0; i < count; i++) {
        PRINTF(cases[i].result, "%s: allocate", cases[i].name);
        PRINTF(cases[i].unchanged, "%s: the free sizes are unchanged", cases[i].name);
    }

    test_exit();
    return 0;
}
