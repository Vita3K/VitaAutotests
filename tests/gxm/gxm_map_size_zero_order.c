// Which mapping sceGxmUnmapMemory removes when a size 0 mapping and a larger one share a base. The
// cases, each on a fresh 1 MiB USER_RW_UNCACHE block, with the mappings made at its start:
// - 64 KiB, then size 0 at the same base
// - size 0, then 64 KiB at the same base
// - size 0 twice, then 64 KiB at the same base
// Each case unmaps the base once, then maps 4 KiB inside the 64 KiB range. That only fails
// (0x805B0017, DRIVER) while the 64 KiB mapping is still there, since a size 0 mapping overlaps
// nothing. The first two cases map the 4 KiB at the base, the third 16 KiB into the range. Then the
// base is unmapped until that fails, printing every unmap, so the count of mappings left shows.
// Everything a case maps is unmapped and its block freed. If a map that a case needs fails, the
// case prints a marker line, skips the probe and only unmaps the base until that fails.
//
// On a PS Vita TV on firmware 3.65 every map works, the size 0 ones and the 64 KiB ones in all
// three cases. The first unmap of the base works, and the 4 KiB map in the range then fails with
// 0x805B0017 (SCE_GXM_ERROR_DRIVER), so the 64 KiB mapping is still there. That holds with the
// size 0 mapping made after the 64 KiB one, before it and twice before it. So the unmap removes a
// size 0 mapping first, not the oldest or the newest one. The unmaps that follow work once more
// in the first two cases and twice in the third, then give 0x805B0017, so each case had as many
// mappings as it made.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)

typedef struct Block {
    SceUID uid;
    char *base;
} Block;

static Block alloc(void) {
    Block block = { .uid = sceKernelAllocMemBlock("gxm size zero order",
                                                  SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, MIB,
                                                  NULL) };
    void *base = NULL;
    if (block.uid >= 0)
        sceKernelGetMemBlockBase(block.uid, &base);
    block.base = base;
    return block;
}

static void display_callback(const void *data) {
    (void)data;
}

static int initialize(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    return sceGxmInitialize(&init);
}

// Unmaps base until that fails, at most 6 times, printing each unmap
static void unmap_all(const char *label, void *base) {
    for (int i = 0; i < 6; i++) {
        const int unmapped = sceGxmUnmapMemory(base);
        PRINTF(unmapped, "%s: unmap the base, try %d", label, i + 1);
        if (unmapped != 0)
            break;
    }
}

// Maps size at base and prints it, and tells if it worked
static int map(const char *label, const char *what, void *base, unsigned size) {
    const int mapped = sceGxmMapMemory(base, size, RW);
    PRINTF(mapped, "%s: %s", label, what);
    return mapped == 0;
}

// Maps the 64 KiB and the size 0 mappings at the base of a block in the given order, 'z' for size
// 0 and 'b' for 64 KiB, and tells if all of them mapped
static int map_all(const char *label, void *base, const char *order) {
    for (int i = 0; order[i] != '\0'; i++) {
        const int big = order[i] == 'b';
        if (!map(label, big ? "map 64 KiB" : "map size 0", base, big ? 64 * KIB : 0))
            return 0;
    }
    return 1;
}

// Unmaps the base once, maps 4 KiB at probe and unmaps it if it mapped, then unmaps the rest
static void probe_and_unmap(const char *label, const Block block, char *probe) {
    PRINTF(sceGxmUnmapMemory(block.base), "%s: unmap the base once", label);
    if (map(label, "map 4 KiB in the range", probe, 4 * KIB))
        PRINTF(sceGxmUnmapMemory(probe), "%s: unmap the 4 KiB", label);
    unmap_all(label, block.base);
}

// Maps in the given order on a fresh block, then probes 4 KiB at probe_offset into it
static void order_case(const char *label, const char *order, unsigned probe_offset) {
    const Block block = alloc();
    if (block.uid < 0 || block.base == NULL) {
        PRINTF(1, "%s: no block", label);
    } else if (map_all(label, block.base, order)) {
        probe_and_unmap(label, block, block.base + probe_offset);
    } else {
        PRINTF(1, "%s: a map failed, skipped", label);
        unmap_all(label, block.base);
    }
    if (block.uid >= 0)
        sceKernelFreeMemBlock(block.uid);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    order_case("64 KiB, then size 0", "bz", 0);
    order_case("size 0, then 64 KiB", "zb", 0);
    order_case("size 0 twice, then 64 KiB", "zzb", 16 * KIB);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
