// sceClib memory space error paths: sizes that overflow, alignments that aren't powers of two or are
// larger than the space, reallocating to and from nothing, a space at an unaligned base, the fast
// stats and the heap empty check. Pointers are printed as alignments and comparisons, never values.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define SPACE_SIZE 0x40000

static char *base;

static int inside(const void *p) {
    return (const char *)p >= base && (const char *)p < base + SPACE_SIZE * 2;
}

// Returns 1 for an allocation in the space aligned to alignment, 0 for NULL, or -1 for one outside it
// or not aligned
static int aligned_in_space(const void *p, SceSize alignment) {
    if (!p)
        return 0;
    return inside(p) && ((SceUIntPtr)p & (alignment - 1)) == 0 ? 1 : -1;
}

int main(void) {
    const SceUID block = sceKernelAllocMemBlock("mspace_edges", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SPACE_SIZE * 2, NULL);
    sceKernelGetMemBlockBase(block, (void **)&base);
    SceClibMspace mspace = sceClibMspaceCreate(base, SPACE_SIZE);
    PRINT("create a space", mspace != NULL);
    PRINT("it is empty", sceClibMspaceIsHeapEmpty(mspace));

    // Stats, slow and fast, before and after an allocation
    SceClibMspaceStats stats = { 0 }, fast = { 0 };
    sceClibMspaceMallocStats(mspace, &stats);
    sceClibMspaceMallocStatsFast(mspace, &fast);
    PRINT("fast stats: capacity", fast.capacity);
    PRINT("fast stats: second capacity field", fast.unk);
    PRINT("fast stats: peak in use", fast.peak_in_use);
    PRINT("fast stats: in use", fast.current_in_use);
    PRINT("the fast stats match the stats", sceClibMemcmp(&stats, &fast, sizeof(stats)) == 0);
    void *one = sceClibMspaceMalloc(mspace, 1000);
    PRINT("allocate 1000 bytes", aligned_in_space(one, 8));
    PRINT("it isn't empty", sceClibMspaceIsHeapEmpty(mspace));
    sceClibMspaceMallocStatsFast(mspace, &fast);
    PRINT("fast stats with it: in use", fast.current_in_use);
    sceClibMspaceFree(mspace, one);
    PRINT("it is empty after freeing it", sceClibMspaceIsHeapEmpty(mspace));

    // Sizes that overflow
    PRINT("calloc 0x10000 of 0x10001 bytes", sceClibMspaceCalloc(mspace, 0x10000, 0x10001) == NULL);
    PRINT("calloc 0x80000000 of 2 bytes", sceClibMspaceCalloc(mspace, 0x80000000u, 2) == NULL);
    void *none = sceClibMspaceCalloc(mspace, 0, 100);
    PRINT("calloc 0 of 100 bytes", aligned_in_space(none, 8));
    sceClibMspaceFree(mspace, none);
    PRINT("malloc 0xFFFFFFFF bytes", sceClibMspaceMalloc(mspace, 0xFFFFFFFFu) == NULL);
    PRINT("malloc 0xFFFFFFF0 bytes", sceClibMspaceMalloc(mspace, 0xFFFFFFF0u) == NULL);
    PRINT("malloc 0x7FFFFFFF bytes", sceClibMspaceMalloc(mspace, 0x7FFFFFFFu) == NULL);
    PRINT("malloc all of the space", sceClibMspaceMalloc(mspace, SPACE_SIZE) == NULL);
    PRINT("it is empty after the failed ones", sceClibMspaceIsHeapEmpty(mspace));
    sceClibMspaceMallocStats(mspace, &stats);
    PRINT("in use after the failed ones", stats.current_in_use);

    // The largest allocation it takes, found by halving
    SceSize low = 0, high = SPACE_SIZE;
    while (high - low > 1) {
        const SceSize mid = low + (high - low) / 2;
        void *p = sceClibMspaceMalloc(mspace, mid);
        if (p)
            low = mid;
        else
            high = mid;
        sceClibMspaceFree(mspace, p);
    }
    PRINT("the largest allocation", low);

    // Alignments
    static const SceSize alignments[] = { 0, 1, 2, 3, 4, 8, 12, 24, 100, 0x8000, 0x40000, 0x80000000u };
    for (unsigned int i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
        const SceSize alignment = alignments[i];
        void *p = sceClibMspaceMemalign(mspace, alignment, 64);
        // An alignment that isn't a power of two is checked against the next one up
        SceSize check = 8;
        while (check < alignment && check < 0x80000000u)
            check <<= 1;
        PRINTF(aligned_in_space(p, check), "memalign to %u", alignment);
        sceClibMspaceFree(mspace, p);
    }

    // Reallocating
    char *p = sceClibMspaceRealloc(mspace, NULL, 64);
    PRINT("realloc from NULL", aligned_in_space(p, 8));
    sceClibMemset(p, 0x3C, 64);
    char *shrunk = sceClibMspaceRealloc(mspace, p, 16);
    PRINT("shrink 64 bytes to 16 keeps the pointer", shrunk == p);
    PRINT("its usable size", sceClibMspaceMallocUsableSize(shrunk));
    PRINT("it keeps the data", shrunk[0] == 0x3C && shrunk[15] == 0x3C);
    char *zero = sceClibMspaceRealloc(mspace, shrunk, 0);
    PRINT("realloc to 0 bytes", aligned_in_space(zero, 8));
    PRINT("it keeps the pointer", zero == shrunk);
    sceClibMspaceFree(mspace, zero);
    char *aligned = sceClibMspaceReallocalign(mspace, NULL, 64, 512);
    PRINT("reallocalign from NULL to 512", aligned_in_space(aligned, 512));
    sceClibMemset(aligned, 0x77, 64);
    char *realigned = sceClibMspaceReallocalign(mspace, aligned, 2000, 4096);
    PRINT("reallocalign it to 2000 bytes at 4096", aligned_in_space(realigned, 4096));
    PRINT("it keeps the data", realigned && realigned[0] == 0x77 && realigned[63] == 0x77);
    char *unchanged = sceClibMspaceReallocalign(mspace, realigned, 2000, 3);
    PRINT("reallocalign it to an alignment of 3", aligned_in_space(unchanged, 4));
    sceClibMspaceFree(mspace, unchanged ? unchanged : realigned);
    PRINT("realloc NULL to 0 bytes", aligned_in_space(sceClibMspaceRealloc(mspace, NULL, 0), 8));
    sceClibMspaceMallocStats(mspace, &stats);
    PRINT("peak in use after all of them", stats.peak_in_use >= SPACE_SIZE / 2);
    sceClibMspaceDestroy(mspace);

    // Spaces of other sizes and bases
    static const SceSize sizes[] = { 0, 0x100, 0x27F, 0x280, 0x2C0, 0x300, 0x1000 };
    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        SceClibMspace small = sceClibMspaceCreate(base, sizes[i]);
        PRINTF(small != NULL, "create a space of 0x%X bytes", sizes[i]);
        if (small) {
            void *q = sceClibMspaceMalloc(small, 8);
            PRINTF(aligned_in_space(q, 8), "allocate 8 bytes from the space of 0x%X bytes", sizes[i]);
            sceClibMspaceDestroy(small);
        }
    }
    static const int offsets[] = { 1, 4, 8, 12 };
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        SceClibMspace shifted = sceClibMspaceCreate(base + offsets[i], SPACE_SIZE);
        PRINTF(shifted != NULL, "create a space %d bytes past the block base", offsets[i]);
        PRINTF(shifted ? (int)(((SceUIntPtr)shifted - (SceUIntPtr)base) & 7) : -1,"its handle's offset past 8 byte alignment, at %d", offsets[i]);
        if (shifted) {
            void *q = sceClibMspaceMalloc(shifted, 100);
            PRINTF(aligned_in_space(q, 8), "allocate 100 bytes from it, at %d", offsets[i]);
            sceClibMspaceDestroy(shifted);
        }
    }

    // NULL pointers last, so an implementation that crashes on them loses nothing else. A space at NULL
    // isn't tried, since a Vita aborts the process for it.
    mspace = sceClibMspaceCreate(base, SPACE_SIZE);
    sceClibMspaceFree(mspace, NULL);
    PRINT("free NULL", sceClibMspaceIsHeapEmpty(mspace));
    PRINT("usable size of NULL", sceClibMspaceMallocUsableSize(NULL));
    sceClibMspaceDestroy(mspace);

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
