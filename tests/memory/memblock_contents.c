// What a new memory block holds, and whether blocks that live together stay apart. Each case makes
// a block of a type, prints whether it starts as all zero, fills it with a byte, makes a second
// block of the same type while the first lives, fills that with another byte, and prints whether
// each still holds only its own byte. Then both end, a third block is made, and whether it starts
// as all zero is printed, since it may take the memory the first one dirtied. The types are
// USER_RW, USER_RW_UNCACHE, USER_CDRAM_RW and USER_MAIN_PHYCONT_RW, 1 MiB each, and last 70 MiB of
// USER_CDRAM_RW, the block Disgaea 3 makes and clears whole. That one has no second block, since
// CDRAM can't hold two, and a range past 2 MiB is only checked in its first and last 256 KiB.
// Everything is printed after the blocks end, so the output file doesn't take memory in between.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000
#define EDGE (256 * KIB)

// Whether a range holds only the byte, or its start and end do for a range past 2 MiB
static int holds_only(const void *base, SceSize size, unsigned char value) {
    const unsigned char *p = base;
    SceSize head = size, tail = 0;
    if (size > 2 * MIB) {
        head = EDGE;
        tail = EDGE;
    }
    for (SceSize i = 0; i < head; i++)
        if (p[i] != value)
            return 0;
    for (SceSize i = 0; i < tail; i++)
        if (p[size - tail + i] != value)
            return 0;
    return 1;
}

typedef struct Result {
    SceUID uid[3];
    int fresh[3];
    int own[2];
} Result;

static void run(const char *name, SceUInt32 type, SceSize size, int two) {
    Result r = { { -1, -1, -1 }, { 0, 0, 0 }, { 0, 0 } };
    void *base[3] = { NULL, NULL, NULL };

    r.uid[0] = sceKernelAllocMemBlock("first", type, size, NULL);
    if (r.uid[0] >= 0 && sceKernelGetMemBlockBase(r.uid[0], &base[0]) == 0) {
        r.fresh[0] = holds_only(base[0], size, 0);
        sceClibMemset(base[0], 0xA5, size);
        if (two) {
            r.uid[1] = sceKernelAllocMemBlock("second", type, size, NULL);
            if (r.uid[1] >= 0 && sceKernelGetMemBlockBase(r.uid[1], &base[1]) == 0) {
                r.fresh[1] = holds_only(base[1], size, 0);
                sceClibMemset(base[1], 0x5A, size);
                r.own[1] = holds_only(base[1], size, 0x5A);
            }
        }
        r.own[0] = holds_only(base[0], size, 0xA5);
    }
    if (r.uid[1] >= 0)
        sceKernelFreeMemBlock(r.uid[1]);
    if (r.uid[0] >= 0)
        sceKernelFreeMemBlock(r.uid[0]);

    r.uid[2] = sceKernelAllocMemBlock("third", type, size, NULL);
    if (r.uid[2] >= 0 && sceKernelGetMemBlockBase(r.uid[2], &base[2]) == 0) {
        r.fresh[2] = holds_only(base[2], size, 0);
        sceKernelFreeMemBlock(r.uid[2]);
    }

    PRINTF(test_created(r.uid[0]), "%s: the first block is made", name);
    PRINTF(r.fresh[0], "%s: the first block starts as zero", name);
    PRINTF(r.own[0], "%s: the first block holds its own byte", name);
    if (two) {
        PRINTF(test_created(r.uid[1]), "%s: the second block is made", name);
        PRINTF(r.fresh[1], "%s: the second block starts as zero", name);
        PRINTF(r.own[1], "%s: the second block holds its own byte", name);
    }
    PRINTF(test_created(r.uid[2]), "%s: the third block is made", name);
    PRINTF(r.fresh[2], "%s: the third block starts as zero", name);
}

int main(void) {
    run("USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MIB, 1);
    run("USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, MIB, 1);
    run("USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, MIB, 1);
    run("USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, MIB, 1);
    run("USER_CDRAM_RW 70 MiB", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 70 * MIB, 0);
    test_exit();
    return 0;
}
