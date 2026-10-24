// How the free sizes from sceKernelGetFreeMemorySize step as blocks are made, with sizes that are
// whole MiB and sizes that aren't. Whether the main, CDRAM and physically contiguous free sizes
// are each a whole number of MiB at the start. Then the test lines the CDRAM and main free sizes up
// at a whole MiB: it makes 256 KiB USER_CDRAM_RW blocks, then 4 KiB USER_RW ones, until the
// reported size drops, and frees the block it dropped at, keeping the others until the end. Where
// the free sizes start within a MiB depends on the build and the console, so the steps would too
// without this. Then for USER_RW blocks of 4 KiB, 512 KiB, 1 MiB less 4 KiB, 1 MiB, 1 MiB and 4
// KiB, 1 MiB and 512 KiB, 2 MiB less 4 KiB, 2 MiB and 16 MiB, and USER_CDRAM_RW blocks of 256 KiB,
// 512 KiB, 768 KiB, 1 MiB, 1 MiB and 256 KiB and 2 MiB, each made alone: the KiB it takes from its
// free size, and whether the free sizes are still whole MiB with it. Then 256 USER_RW blocks of 4
// KiB, 2 of 512 KiB and 4 USER_CDRAM_RW blocks of 256 KiB, each set live at once, with the same.
// Last, whether every free size is back to what it was at the start once the blocks that line them
// up are freed. Everything is measured first and printed after, so the output file the test
// writes doesn't take memory in between.
//
// On a PS Vita TV on firmware 3.65 every free size is a whole number of MiB at the start and with
// every block. With the free sizes lined up, every lone block takes its size rounded up to a whole
// MiB: 4 KiB up to 1 MiB take 1 MiB, more up to 2 MiB take 2 MiB, and 16 MiB takes 16 MiB, and
// CDRAM steps the same way. 2 blocks of 512 KiB and 4 CDRAM blocks of 256 KiB live at once take 1
// MiB, but 256 blocks of 4 KiB take 2 MiB. Every free size is back after the frees.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define MAX_BLOCKS 256
// The most blocks lining up the two free sizes takes: 4 of CDRAM and 256 of main
#define MAX_PADS 260
#define MAX_RESULTS 32

static SceUID blocks[MAX_BLOCKS];
static SceUID pads[MAX_PADS];
static int pad_count;

// What one take measured, printed at the end
typedef struct {
    const char *label;
    int error;
    int cdram;
    int taken;
    int whole;
} Result;

static Result results[MAX_RESULTS];
static int result_count;

static SceKernelFreeMemorySizeInfo free_sizes(void) {
    SceKernelFreeMemorySizeInfo info = { .size = sizeof(info) };
    sceKernelGetFreeMemorySize(&info);
    return info;
}

static int whole(const SceKernelFreeMemorySizeInfo *info) {
    return info->size_user % MIB == 0 && info->size_cdram % MIB == 0
        && info->size_phycont % MIB == 0;
}

static SceSize free_of(const SceKernelFreeMemorySizeInfo *info, int cdram) {
    return cdram ? info->size_cdram : info->size_user;
}

// Makes blocks of the type and size until the reported free size drops, then frees the block it
// dropped at, so the free size is a whole MiB to the page. Returns 0 or the error.
static int line_up(SceUInt32 type, SceSize size) {
    const int cdram = type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    const SceKernelFreeMemorySizeInfo start = free_sizes();
    while (pad_count < MAX_PADS) {
        const SceUID block = sceKernelAllocMemBlock("pad", type, size, NULL);
        if (block < 0)
            return block;
        const SceKernelFreeMemorySizeInfo now = free_sizes();
        if (free_of(&now, cdram) < free_of(&start, cdram))
            return sceKernelFreeMemBlock(block);
        pads[pad_count++] = block;
    }
    return -1;
}

// Makes count blocks of the type and size at once, and records the KiB they take from the main or
// CDRAM free size and whether the free sizes stay whole MiB
static void take(const char *label, SceUInt32 type, SceSize size, int count) {
    Result *result = &results[result_count++];
    result->label = label;
    result->cdram = type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    const SceKernelFreeMemorySizeInfo before = free_sizes();
    result->error = 0;
    for (int i = 0; i < count; i++) {
        blocks[i] = sceKernelAllocMemBlock("steps", type, size, NULL);
        if (blocks[i] < 0 && result->error == 0)
            result->error = blocks[i];
    }
    const SceKernelFreeMemorySizeInfo during = free_sizes();
    for (int i = 0; i < count; i++)
        if (blocks[i] >= 0)
            sceKernelFreeMemBlock(blocks[i]);
    result->taken = free_of(&before, result->cdram) - free_of(&during, result->cdram);
    result->whole = whole(&during);
}

int main(void) {
    const SceKernelFreeMemorySizeInfo start = free_sizes();
    const int lined_up_cdram = line_up(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB);
    const int lined_up_main = line_up(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB);

    static const struct {
        const char *label;
        SceSize size;
    } main_sizes[] = {
        { "USER_RW, 4 KiB", 4 * KIB },
        { "USER_RW, 512 KiB", 512 * KIB },
        { "USER_RW, 1 MiB less 4 KiB", MIB - 4 * KIB },
        { "USER_RW, 1 MiB", MIB },
        { "USER_RW, 1 MiB and 4 KiB", MIB + 4 * KIB },
        { "USER_RW, 1 MiB and 512 KiB", MIB + 512 * KIB },
        { "USER_RW, 2 MiB less 4 KiB", 2 * MIB - 4 * KIB },
        { "USER_RW, 2 MiB", 2 * MIB },
        { "USER_RW, 16 MiB", 16 * MIB },
    };
    for (unsigned int i = 0; i < sizeof(main_sizes) / sizeof(main_sizes[0]); i++)
        take(main_sizes[i].label, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, main_sizes[i].size, 1);

    static const struct {
        const char *label;
        SceSize size;
    } cdram_sizes[] = {
        { "USER_CDRAM_RW, 256 KiB", 256 * KIB },
        { "USER_CDRAM_RW, 512 KiB", 512 * KIB },
        { "USER_CDRAM_RW, 768 KiB", 768 * KIB },
        { "USER_CDRAM_RW, 1 MiB", MIB },
        { "USER_CDRAM_RW, 1 MiB and 256 KiB", MIB + 256 * KIB },
        { "USER_CDRAM_RW, 2 MiB", 2 * MIB },
    };
    for (unsigned int i = 0; i < sizeof(cdram_sizes) / sizeof(cdram_sizes[0]); i++)
        take(cdram_sizes[i].label, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, cdram_sizes[i].size, 1);

    take("256 USER_RW blocks of 4 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, 256);
    take("2 USER_RW blocks of 512 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 512 * KIB, 2);
    take("4 USER_CDRAM_RW blocks of 256 KiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, 4);

    for (int i = 0; i < pad_count; i++)
        sceKernelFreeMemBlock(pads[i]);
    const SceKernelFreeMemorySizeInfo end = free_sizes();

    PRINT("the main free size is whole MiB", start.size_user % MIB == 0);
    PRINT("the CDRAM free size is whole MiB", start.size_cdram % MIB == 0);
    PRINT("the physically contiguous free size is whole MiB", start.size_phycont % MIB == 0);
    PRINT("line up the CDRAM free size", lined_up_cdram);
    PRINT("line up the main free size", lined_up_main);
    for (int i = 0; i < result_count; i++) {
        const Result *result = &results[i];
        PRINTF(result->error, "%s: make it", result->label);
        if (result->error < 0)
            continue;
        PRINTF(result->taken / KIB, "%s: KiB taken from %s", result->label, result->cdram ? "CDRAM" : "main");
        PRINTF(result->whole, "%s: the free sizes are whole MiB with it", result->label);
    }
    PRINT("every free size is back to the start",
        end.size_user == start.size_user && end.size_cdram == start.size_cdram
            && end.size_phycont == start.size_phycont);
    test_exit();
    return 0;
}
