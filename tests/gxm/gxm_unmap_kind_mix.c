// Which record sceGxmUnmapVertexUsseMemory and sceGxmUnmapFragmentUsseMemory remove when a
// sceGxmMapMemory mapping and a USSE mapping share a base. Each case runs on a fresh 64 KiB
// USER_RW block, nothing reads or writes it, and every call prints its code. sceGxmMapMemory maps
// the whole 64 KiB and a USSE map is 4 KiB. The cases:
// - sceGxmMapMemory at the base, then a vertex USSE map at the base. Then unmap the vertex USSE
//   base, unmap the memory base, unmap the vertex USSE base again, and one more unmap of each
//   kind to count what is left.
// - the same with the vertex USSE map first and sceGxmMapMemory second
// - the same as the first case with the fragment USSE kind
// - control, sceGxmMapMemory at the base and a vertex USSE map 32 KiB into the block. Then unmap
//   the vertex USSE map there and unmap the memory base.
// After each unmap that follows a USSE unmap, and after the last one, a probe maps 4 KiB with
// sceGxmMapMemory 48 KiB into the block. That fails with 0x805B0017 (SCE_GXM_ERROR_DRIVER) while
// a sceGxmMapMemory mapping still covers it, and when it maps it is unmapped again.
// If a block can't be made the case prints a fixed line and skips the rest. Anything a case
// mapped is unmapped when it ends, and if the block can't be freed after that it is freed again
// once all mappings are unmapped.
//
// On a PS Vita TV on firmware 3.65, with the sceGxmMapMemory mapping made first, the USSE unmap
// at the shared base removes that mapping and not its own. The probe then maps, the
// sceGxmUnmapMemory that follows fails with 0x805B0017 (SCE_GXM_ERROR_DRIVER), and the second USSE
// unmap removes the USSE mapping with 0. The vertex and fragment kinds do the same. With the USSE
// mapping made first, each unmap removes its own mapping: the probe fails with 0x805B0017 until
// sceGxmUnmapMemory, and the second USSE unmap fails with 0x805B0017. In the control case, with no
// shared base, each unmap removes its own mapping too. The leftover unmaps fail with 0x805B0017 and
// the last probes map in every case.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)

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

static Block alloc(void) {
    Block block = { .uid = sceKernelAllocMemBlock("gxm unmap kind mix",
                                                  SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL) };
    void *base = NULL;
    if (block.uid >= 0 && (sceKernelGetMemBlockBase(block.uid, &base) < 0 || !base)) {
        sceKernelFreeMemBlock(block.uid);
        block.uid = -1;
        base = NULL;
    }
    block.base = base;
    return block;
}

static int usse_map(int fragment, char *base) {
    SceUInt32 offset = 0;
    return fragment ? sceGxmMapFragmentUsseMemory(base, 4 * KIB, &offset)
                    : sceGxmMapVertexUsseMemory(base, 4 * KIB, &offset);
}

static int usse_unmap(int fragment, char *base) {
    return fragment ? sceGxmUnmapFragmentUsseMemory(base) : sceGxmUnmapVertexUsseMemory(base);
}

// Removes any mapping left at the block, then frees it. A free of a mapped block fails and
// leaves it allocated for good, so the unmaps come first.
static void free_block(Block block, char *usse_base) {
    if (block.uid < 0)
        return;
    for (int i = 0; i < 4; i++) {
        sceGxmUnmapMemory(block.base);
        sceGxmUnmapVertexUsseMemory(usse_base);
        sceGxmUnmapFragmentUsseMemory(usse_base);
    }
    sceKernelFreeMemBlock(block.uid);
}

// Maps 4 KiB 48 KiB into the block, and unmaps it if it mapped
static void probe(const char *label, Block block) {
    char *at = block.base + 48 * KIB;
    const int mapped = sceGxmMapMemory(at, 4 * KIB, RW);
    PRINTF(mapped, "%s: probe map", label);
    if (mapped == 0)
        PRINTF(sceGxmUnmapMemory(at), "%s: probe unmap", label);
}

// A memory map and a USSE map at the base, in the given order, then the unmaps and probes
static void mix_case(const char *label, int fragment, int usse_first) {
    const Block block = alloc();
    if (block.uid < 0) {
        PRINTF(1, "%s: no block", label);
        return;
    }
    char *base = block.base;
    for (int i = 0; i < 2; i++) {
        if ((i == 0) == usse_first)
            PRINTF(usse_map(fragment, base), "%s: map USSE", label);
        else
            PRINTF(sceGxmMapMemory(base, SIZE, RW), "%s: map memory", label);
    }
    PRINTF(usse_unmap(fragment, base), "%s: unmap USSE", label);
    probe(label, block);
    PRINTF(sceGxmUnmapMemory(base), "%s: unmap memory", label);
    PRINTF(usse_unmap(fragment, base), "%s: unmap USSE again", label);
    probe(label, block);
    PRINTF(sceGxmUnmapMemory(base), "%s: unmap memory, leftover check", label);
    PRINTF(usse_unmap(fragment, base), "%s: unmap USSE, leftover check", label);
    probe(label, block);
    free_block(block, base);
    PRINTF(0, "%s: done", label);
}

// The USSE map is 32 KiB into the block, so the two don't share a base
static void control_case(const char *label) {
    const Block block = alloc();
    if (block.uid < 0) {
        PRINTF(1, "%s: no block", label);
        return;
    }
    char *usse = block.base + 32 * KIB;
    PRINTF(sceGxmMapMemory(block.base, SIZE, RW), "%s: map memory", label);
    PRINTF(usse_map(0, usse), "%s: map USSE", label);
    PRINTF(usse_unmap(0, usse), "%s: unmap USSE", label);
    probe(label, block);
    PRINTF(sceGxmUnmapMemory(block.base), "%s: unmap memory", label);
    probe(label, block);
    free_block(block, usse);
    PRINTF(0, "%s: done", label);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    mix_case("memory first, vertex", 0, 0);
    mix_case("USSE first, vertex", 0, 1);
    mix_case("memory first, fragment", 1, 0);
    control_case("control");
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
