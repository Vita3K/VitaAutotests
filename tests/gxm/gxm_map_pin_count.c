// How many pins a block gets from GPU mappings. Every case has a fresh 64 KiB USER_RW block. The
// cases:
// - the two 32 KiB halves mapped with sceGxmMapMemory, the first unmapped, the free, the second
//   unmapped
// - the whole block mapped with sceGxmMapMemory and 4 KiB of it with sceGxmMapVertexUsseMemory,
//   the MapMemory mapping unmapped, the free, the USSE unmap
// - sceGxmMapMemory with size 0 twice at the base, one unmapped, the free, the other unmapped
// - map, unmap, map, unmap, then the free
// - mapped with sceGxmMapMemory, then sceGxmTerminate, then the free. This case is last, since it
//   terminates libgxm.
// The block's base is looked up with sceKernelFindMemBlockByAddr after the free, and in the first
// three cases again after the last unmap. A find prints 0 when it gives a block, and the error when
// it fails. A block the last find of a case still gives is freed with no print.
//
// On a PS Vita TV on firmware 3.65 every map and unmap gives 0. In the first three cases the
// free gives 0x80024703 (BLOCK_IN_USE) after the first unmap, so the other mapping still holds the
// block. This is the same with two halves, with MapMemory plus a vertex USSE mapping, and with
// two size 0 maps. The find after the free and the find after the last unmap both give 0x80024501
// (INVALID_UID), so the block is not found by its address even after the last unmap. In the map,
// unmap, map, unmap case the free gives 0, and the find gives 0x80022007 (VA2PA_FAULT). The block
// mapped at sceGxmTerminate frees with 0 as well, and the find after it gives 0x80022007.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)

typedef struct Block {
    const char *label;
    SceUID uid;
    char *base;
} Block;

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

// Makes the case's block, returns 0 and prints a marker if that failed
static int make(Block *block, const char *label) {
    block->label = label;
    block->base = NULL;
    block->uid = sceKernelAllocMemBlock("gxm pin count", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    PRINTF(test_created(block->uid), "%s: allocate it", label);
    if (block->uid < 0)
        return 0;
    void *base = NULL;
    const int got = sceKernelGetMemBlockBase(block->uid, &base);
    PRINTF(got, "%s: its base", label);
    if (got != 0) {
        sceKernelFreeMemBlock(block->uid);
        return 0;
    }
    block->base = base;
    return 1;
}

static SceUID find(const Block *block, const char *when) {
    const SceUID found = sceKernelFindMemBlockByAddr(block->base, 0);
    PRINTF(found < 0 ? found : 0, "%s, %s: find by its base", block->label, when);
    return found;
}

// The last find of a case, frees the block if it is still there
static void find_last(const Block *block, const char *when) {
    const SceUID found = find(block, when);
    if (found >= 0)
        sceKernelFreeMemBlock(found);
}

// The block is freed by the case itself, this only drops it if the case stopped early
static void drop(const Block *block) {
    sceKernelFreeMemBlock(block->uid);
}

static void two_halves(void) {
    Block block;
    if (!make(&block, "two halves"))
        return;
    const int first = sceGxmMapMemory(block.base, SIZE / 2, RW);
    PRINT("two halves: map the first half", first);
    const int second = sceGxmMapMemory(block.base + SIZE / 2, SIZE / 2, RW);
    PRINT("two halves: map the second half", second);
    if (first == 0 && second == 0) {
        PRINT("two halves: unmap the first half", sceGxmUnmapMemory(block.base));
        PRINT("two halves: free it", sceKernelFreeMemBlock(block.uid));
        find(&block, "after the free");
        PRINT("two halves: unmap the second half", sceGxmUnmapMemory(block.base + SIZE / 2));
        find_last(&block, "after the last unmap");
        return;
    }
    if (first == 0)
        sceGxmUnmapMemory(block.base);
    if (second == 0)
        sceGxmUnmapMemory(block.base + SIZE / 2);
    drop(&block);
}

static void memory_and_usse(void) {
    Block block;
    if (!make(&block, "MapMemory and USSE"))
        return;
    SceUInt32 offset = 0;
    const int memory = sceGxmMapMemory(block.base, SIZE, RW);
    PRINT("MapMemory and USSE: map the block", memory);
    const int usse = sceGxmMapVertexUsseMemory(block.base, 4 * KIB, &offset);
    PRINT("MapMemory and USSE: map 4 KiB of it as vertex USSE", usse);
    if (memory == 0 && usse == 0) {
        PRINT("MapMemory and USSE: unmap the block", sceGxmUnmapMemory(block.base));
        PRINT("MapMemory and USSE: free it", sceKernelFreeMemBlock(block.uid));
        find(&block, "after the free");
        PRINT("MapMemory and USSE: unmap the vertex USSE mapping",
              sceGxmUnmapVertexUsseMemory(block.base));
        find_last(&block, "after the last unmap");
        return;
    }
    if (memory == 0)
        sceGxmUnmapMemory(block.base);
    if (usse == 0)
        sceGxmUnmapVertexUsseMemory(block.base);
    drop(&block);
}

static void size_zero_twice(void) {
    Block block;
    if (!make(&block, "size 0 twice"))
        return;
    const int first = sceGxmMapMemory(block.base, 0, RW);
    PRINT("size 0 twice: map size 0 the first time", first);
    const int second = sceGxmMapMemory(block.base, 0, RW);
    PRINT("size 0 twice: map size 0 the second time", second);
    if (first == 0 && second == 0) {
        PRINT("size 0 twice: unmap one", sceGxmUnmapMemory(block.base));
        PRINT("size 0 twice: free it", sceKernelFreeMemBlock(block.uid));
        find(&block, "after the free");
        PRINT("size 0 twice: unmap the other", sceGxmUnmapMemory(block.base));
        find_last(&block, "after the last unmap");
        return;
    }
    if (first == 0)
        sceGxmUnmapMemory(block.base);
    if (second == 0)
        sceGxmUnmapMemory(block.base);
    drop(&block);
}

static void map_twice_in_turn(void) {
    Block block;
    if (!make(&block, "map, unmap, map, unmap"))
        return;
    int mapped = 0;
    for (int i = 0; i < 2; i++) {
        const int result = sceGxmMapMemory(block.base, SIZE, RW);
        PRINTF(result, "map, unmap, map, unmap: map it, round %d", i + 1);
        if (result != 0)
            break;
        PRINTF(sceGxmUnmapMemory(block.base), "map, unmap, map, unmap: unmap it, round %d", i + 1);
        mapped++;
    }
    if (mapped == 2) {
        PRINT("map, unmap, map, unmap: free it", sceKernelFreeMemBlock(block.uid));
        find_last(&block, "after the free");
        return;
    }
    drop(&block);
}

// Last, since it terminates libgxm
static void terminated(void) {
    Block block;
    if (!make(&block, "mapped at the terminate")) {
        sceGxmTerminate();
        return;
    }
    const int mapped = sceGxmMapMemory(block.base, SIZE, RW);
    PRINT("mapped at the terminate: map it", mapped);
    PRINT("mapped at the terminate: terminate libgxm", sceGxmTerminate());
    if (mapped == 0) {
        PRINT("mapped at the terminate: free it", sceKernelFreeMemBlock(block.uid));
        find_last(&block, "after the free");
        return;
    }
    drop(&block);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    two_halves();
    memory_and_usse();
    size_zero_twice();
    map_twice_in_turn();
    terminated();
    test_exit();
    return 0;
}
