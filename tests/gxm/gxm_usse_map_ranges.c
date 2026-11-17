// Ranges given to sceGxmMapVertexUsseMemory and sceGxmMapFragmentUsseMemory that test the block
// under them, the checks gxm_map_memory and gxm_map_no_block make for sceGxmMapMemory. No GPU work
// runs. For each kind:
// - a 64 KiB range at the base of a 32 KiB block, so it ends 32 KiB past the block
// - a range in no block, the range of a block that was freed
// - a whole USER_MAIN_DEVICE_RW block, a whole USER_CDRAM_RW block and a whole USER_MAIN_R block
// - a size of 6 KiB at the base of a 64 KiB block
// A range over two blocks next to each other is in gxm_usse_map_neighbors, since a Vita doesn't
// promise such a pair. Every map that works is unmapped again and every block is freed.
//
// On a PS Vita TV on firmware 3.65 both kinds give the same results. The 64 KiB range over the
// 32 KiB block, the range of the freed block, the whole USER_MAIN_DEVICE_RW block and the 6 KiB
// size fail with 0x805B0017 (SCE_GXM_ERROR_DRIVER). The whole USER_CDRAM_RW block and the whole
// USER_MAIN_R block map and unmap with 0.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400

// The calls of one USSE kind
typedef struct Kind {
    const char *name;
    int (*map)(void *base, SceSize size, unsigned int *offset);
    int (*unmap)(void *base);
} Kind;

static const Kind kinds[] = {
    { "vertex", sceGxmMapVertexUsseMemory, sceGxmUnmapVertexUsseMemory },
    { "fragment", sceGxmMapFragmentUsseMemory, sceGxmUnmapFragmentUsseMemory },
};

typedef struct Block {
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

static Block alloc(SceUInt32 type, SceSize size) {
    Block block = { sceKernelAllocMemBlock("gxm usse", type, size, NULL), NULL };
    void *base = NULL;
    if (block.uid >= 0 && sceKernelGetMemBlockBase(block.uid, &base) == 0)
        block.base = base;
    return block;
}

// Maps, prints the result and unmaps a mapping that worked, printing that too
static void map_once(const Kind *kind, const char *label, void *base, SceSize size) {
    unsigned int offset = 0;
    const int mapped = kind->map(base, size, &offset);
    PRINTF(mapped, "%s USSE: %s", kind->name, label);
    if (mapped == 0)
        PRINTF(kind->unmap(base), "%s USSE: %s: unmap it", kind->name, label);
}

// Allocates a block, maps all of it and frees it
static void map_block(const Kind *kind, const char *label, SceUInt32 type, SceSize size) {
    const Block block = alloc(type, size);
    PRINTF(test_created(block.uid), "%s USSE: %s: allocate it", kind->name, label);
    if (block.uid < 0)
        return;
    if (block.base)
        map_once(kind, label, block.base, size);
    else
        PRINTF(0, "%s USSE: %s: get its base", kind->name, label);
    sceKernelFreeMemBlock(block.uid);
}

static void past_the_end(const Kind *kind) {
    const Block block = alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 32 * KIB);
    PRINTF(test_created(block.uid), "%s USSE: 64 KiB over a 32 KiB block: allocate it", kind->name);
    if (block.uid < 0)
        return;
    if (block.base)
        map_once(kind, "64 KiB over a 32 KiB block", block.base, 64 * KIB);
    else
        PRINTF(0, "%s USSE: 64 KiB over a 32 KiB block: get its base", kind->name);
    sceKernelFreeMemBlock(block.uid);
}

static void freed_range(const Kind *kind) {
    const Block block = alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * KIB);
    PRINTF(test_created(block.uid), "%s USSE: a freed range: allocate the block", kind->name);
    if (block.uid < 0)
        return;
    char *const base = block.base;
    const int freed = sceKernelFreeMemBlock(block.uid);
    PRINTF(freed, "%s USSE: a freed range: free the block", kind->name);
    if (!base)
        PRINTF(0, "%s USSE: a freed range: get its base", kind->name);
    else if (freed == 0)
        map_once(kind, "a freed range", base, 64 * KIB);
}

static void unaligned_size(const Kind *kind) {
    const Block block = alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * KIB);
    PRINTF(test_created(block.uid), "%s USSE: 6 KiB: allocate the block", kind->name);
    if (block.uid < 0)
        return;
    if (block.base)
        map_once(kind, "6 KiB", block.base, 6 * KIB);
    else
        PRINTF(0, "%s USSE: 6 KiB: get its base", kind->name);
    sceKernelFreeMemBlock(block.uid);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    for (unsigned int i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        const Kind *kind = &kinds[i];
        past_the_end(kind);
        freed_range(kind);
        map_block(kind, "a whole USER_MAIN_DEVICE_RW block", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_DEVICE_RW, 4 * KIB);
        map_block(kind, "a whole USER_CDRAM_RW block", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * KIB);
        map_block(kind, "a whole USER_MAIN_R block", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 64 * KIB);
        unaligned_size(kind);
    }
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
