// The common dialog partition: how many 1 MiB USER_CDIALOG_RW blocks it holds before one fails, and
// what the other common dialog types do while it is full. The common dialog types take nothing from
// the main, CDRAM and phycont free sizes, so they draw from a partition of their own. The cases:
// - 1 MiB USER_CDIALOG_RW blocks made until one fails, or until 1024 are made: the error of the one
//   that fails, whether the cap was reached, and whether the count reaches a few thresholds
// - whether the main, CDRAM and phycont free sizes stay the same while the partition fills
// - with the partition full, one 1 MiB block of each other common dialog type: the error of each,
//   and whether it is the error that filled the partition, which means one partition for all
// - after freeing one of the full partition's blocks, whether one 1 MiB block of each other type
//   fits in the freed MiB, then whether 4 KiB USER_CDIALOG_RW blocks fill it exactly
// - whether a CDRAM block and an uncached common dialog block share an address
//
// Only thresholds of the count are printed, since the partition's size is not in the firmware
// notes. All results are printed once every block is freed, since the output may need memory.
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define CAP 1024
#define SMALL_CAP 512
#define SHARED 4

// Sizes in MiB that the count of 1 MiB blocks is compared with
static const int thresholds[] = { 4, 32, 128 };

static SceUID pool[CAP];
static SceUID small[SMALL_CAP];

static SceKernelFreeMemorySizeInfo free_sizes(void) {
    SceKernelFreeMemorySizeInfo info = { .size = sizeof(info) };
    sceKernelGetFreeMemorySize(&info);
    return info;
}

static SceUIntPtr base_of(SceUID block) {
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    return (SceUIntPtr)base;
}

// Makes 1 MiB USER_CDIALOG_RW blocks into pool until one fails or the cap is reached, and returns
// how many were made. The error of the one that failed is stored in error, or 0 at the cap.
static int fill_pool(SceInt32 *error) {
    int made = 0;
    *error = 0;
    while (made < CAP) {
        const SceUID block
            = sceKernelAllocMemBlock("cdialog pool", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, MIB, NULL);
        if (block < 0) {
            *error = block;
            break;
        }
        pool[made++] = block;
    }
    return made;
}

// The common dialog types other than USER_CDIALOG_RW, in the order they are tried
static const struct {
    const char *name;
    SceUInt32 type;
} others[] = {
    { "USER_CDIALOG_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW },
    { "USER_MAIN_CDIALOG_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_RW },
    { "USER_MAIN_CDIALOG_NC_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_NC_RW },
    { "USER_CDIALOG_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_R },
    { "USER_CDIALOG_NC_R", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_R },
};
#define OTHERS (sizeof(others) / sizeof(others[0]))

// Whether a CDRAM block and an uncached common dialog block, both made here, share an address
static int cdram_and_dialog_share(void) {
    SceUID cdram[SHARED], dialog[SHARED];
    SceUIntPtr cdram_base[SHARED], dialog_base[SHARED];
    for (int i = 0; i < SHARED; i++) {
        cdram[i]
            = sceKernelAllocMemBlock("cdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB, NULL);
        dialog[i]
            = sceKernelAllocMemBlock("dialog", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW, MIB, NULL);
        cdram_base[i] = cdram[i] >= 0 ? base_of(cdram[i]) : 0;
        dialog_base[i] = dialog[i] >= 0 ? base_of(dialog[i]) : 0;
    }
    int shared = 0;
    for (int i = 0; i < SHARED; i++) {
        for (int j = 0; j < SHARED; j++) {
            if (cdram[i] < 0 || dialog[j] < 0)
                continue;
            shared |= cdram_base[i] < dialog_base[j] + MIB && dialog_base[j] < cdram_base[i] + 256 * KIB;
        }
    }
    for (int i = 0; i < SHARED; i++) {
        if (cdram[i] >= 0)
            sceKernelFreeMemBlock(cdram[i]);
        if (dialog[i] >= 0)
            sceKernelFreeMemBlock(dialog[i]);
    }
    return shared;
}

int main(void) {
    const SceKernelFreeMemorySizeInfo before = free_sizes();
    SceInt32 full_error = 0;
    const int made = fill_pool(&full_error);
    const SceKernelFreeMemorySizeInfo filled = free_sizes();
    const int unchanged = before.size_user == filled.size_user && before.size_cdram == filled.size_cdram
        && before.size_phycont == filled.size_phycont;

    // With the partition full, one block of each other type
    SceInt32 other_errors[OTHERS];
    for (unsigned int i = 0; i < OTHERS; i++) {
        const SceUID block = sceKernelAllocMemBlock("other", others[i].type, MIB, NULL);
        other_errors[i] = block < 0 ? block : 0;
        if (block >= 0)
            sceKernelFreeMemBlock(block);
    }

    // Freeing one block of the full partition, then one 1 MiB block of each type. The blocks are
    // kept until all are tried, so the first type that fits takes the freed MiB and the rest should
    // fail if they share the partition.
    int fits_rw = 0;
    int fits[OTHERS] = { 0 };
    SceUID taken[OTHERS];
    int small_count = 0;
    if (made > 0) {
        sceKernelFreeMemBlock(pool[0]);
        const SceUID again
            = sceKernelAllocMemBlock("again", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, MIB, NULL);
        fits_rw = again >= 0;
        if (again >= 0)
            sceKernelFreeMemBlock(again);
        for (unsigned int i = 0; i < OTHERS; i++) {
            taken[i] = sceKernelAllocMemBlock("fit again", others[i].type, MIB, NULL);
            fits[i] = taken[i] >= 0;
        }
        for (unsigned int i = 0; i < OTHERS; i++) {
            if (taken[i] >= 0) {
                sceKernelFreeMemBlock(taken[i]);
                taken[i] = -1;
            }
        }
        // The freed MiB is free again, so 4 KiB blocks of the same type fill it or not
        while (small_count < SMALL_CAP) {
            const SceUID block
                = sceKernelAllocMemBlock("granule", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_RW, 4 * KIB, NULL);
            if (block < 0)
                break;
            small[small_count++] = block;
        }
    }

    for (int i = 0; i < small_count; i++)
        sceKernelFreeMemBlock(small[i]);
    for (int i = 1; i < made; i++)
        sceKernelFreeMemBlock(pool[i]);
    const int shared = cdram_and_dialog_share();

    PRINT("the error when the partition is full", full_error);
    for (unsigned int i = 0; i < sizeof(thresholds) / sizeof(thresholds[0]); i++)
        PRINTF(made >= thresholds[i], "at least %d MiB of USER_CDIALOG_RW fit", thresholds[i]);
    PRINT("the cap of 1024 blocks is not reached", made < CAP);
    PRINT("main, CDRAM and phycont free sizes are unchanged while it fills", unchanged);
    for (unsigned int i = 0; i < OTHERS; i++) {
        PRINTF(other_errors[i], "%s, 1 MiB, partition full: the error", others[i].name);
        PRINTF(full_error != 0 && other_errors[i] == full_error,
               "%s, 1 MiB, partition full: the same error as the full one", others[i].name);
    }
    PRINT("USER_CDIALOG_RW, 1 MiB: fits again after freeing one block", fits_rw);
    for (unsigned int i = 0; i < OTHERS; i++)
        PRINTF(fits[i], "%s, 1 MiB: fits in the freed MiB", others[i].name);
    PRINT("4 KiB USER_CDIALOG_RW blocks: at least one fits in the freed MiB", small_count >= 1);
    PRINT("4 KiB USER_CDIALOG_RW blocks: exactly 256 fit in the freed MiB", small_count == 256);
    PRINT("a CDRAM block and an uncached common dialog block share an address", shared);
    test_exit();
    return 0;
}
