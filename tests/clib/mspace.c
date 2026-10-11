// sceClib memory spaces on a memory block: creating one, what allocations return, their usable sizes
// and alignment, and the stats. Pointers are printed as alignments and comparisons, never values.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB 0x100000

static char *base;

// True if p lies inside the memory block
static int inside(const void *p) {
    return (const char *)p >= base && (const char *)p < base + MIB;
}

static void print_stats(const char *what, SceClibMspace mspace) {
    SceClibMspaceStats stats = { 0 };
    sceClibMspaceMallocStats(mspace, &stats);
    PRINTF(stats.capacity, "%s: capacity", what);
    PRINTF(stats.unk, "%s: second capacity field", what);
    PRINTF(stats.peak_in_use, "%s: peak in use", what);
    PRINTF(stats.current_in_use, "%s: in use", what);
}

int main(void) {
    const SceUID block = sceKernelAllocMemBlock("mspace", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, MIB, NULL);
    sceKernelGetMemBlockBase(block, (void **)&base);
    const SceClibMspace mspace = sceClibMspaceCreate(base, MIB);
    PRINT("create a memory space", mspace != NULL);
    PRINT("it lives in the block", inside(mspace));
    print_stats("new", mspace);

    // Allocations
    char *a = sceClibMspaceMalloc(mspace, 100);
    PRINT("allocate 100 bytes", a != NULL && inside(a));
    PRINT("it is 8 byte aligned", ((SceUIntPtr)a & 7) == 0);
    PRINT("it is 16 byte aligned", ((SceUIntPtr)a & 15) == 0);
    PRINT("its usable size", sceClibMspaceMallocUsableSize(a));
    char *zero = sceClibMspaceMalloc(mspace, 0);
    PRINT("allocate 0 bytes", zero != NULL);
    PRINT("its usable size", zero ? (int)sceClibMspaceMallocUsableSize(zero) : -1);
    static const SceSize sizes[] = { 1, 8, 9, 24, 25, 4096, 65536 };
    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        void *p = sceClibMspaceMalloc(mspace, sizes[i]);
        PRINTF(p ? (int)sceClibMspaceMallocUsableSize(p) : -1, "usable size of %u bytes", sizes[i]);
        sceClibMspaceFree(mspace, p);
    }
    print_stats("with two allocations", mspace);

    // Zeroing, growing and aligning
    sceClibMemset(a, 0x5A, 100);
    unsigned char *c = sceClibMspaceCalloc(mspace, 10, 10);
    int zeroed = c != NULL;
    for (int i = 0; c && i < 100; i++)
        zeroed &= c[i] == 0;
    PRINT("calloc zeroes", zeroed);
    char *grown = sceClibMspaceRealloc(mspace, a, 5000);
    PRINT("grow the 100 bytes to 5000", grown != NULL);
    int kept = grown != NULL;
    for (int i = 0; grown && i < 100; i++)
        kept &= grown[i] == 0x5A;
    PRINT("it keeps the data", kept);
    static const SceSize alignments[] = { 16, 64, 4096 };
    for (unsigned int i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
        void *p = sceClibMspaceMemalign(mspace, alignments[i], 100);
        PRINTF(p != NULL && ((SceUIntPtr)p & (alignments[i] - 1)) == 0, "allocate aligned to %u", alignments[i]);
        sceClibMspaceFree(mspace, p);
    }
    void *ra = sceClibMspaceReallocalign(mspace, NULL, 100, 256);
    PRINT("realloc aligned from nothing", ra != NULL && ((SceUIntPtr)ra & 255) == 0);
    sceClibMspaceFree(mspace, ra);

    // Running out
    PRINT("allocate more than the space holds", sceClibMspaceMalloc(mspace, 2 * MIB) == NULL);
    PRINT("realloc to more than the space holds", sceClibMspaceRealloc(mspace, grown, 2 * MIB) == NULL);

    // Freeing everything
    sceClibMspaceFree(mspace, grown);
    sceClibMspaceFree(mspace, c);
    sceClibMspaceFree(mspace, zero);
    sceClibMspaceFree(mspace, NULL);
    print_stats("after freeing everything", mspace);
    sceClibMspaceDestroy(mspace);

    // A space too small for itself
    PRINT("create a space of 64 bytes", sceClibMspaceCreate(base, 64) != NULL);

    test_exit();
    return 0;
}
