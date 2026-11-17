// What the offsets that sceGxmMapVertexUsseMemory and sceGxmMapFragmentUsseMemory give look like.
// Only relations are printed, never an offset. Each case starts with sceGxmTerminate and
// sceGxmInitialize, so every USSE heap is new, and uses fresh blocks that are unmapped and freed
// after it. For the vertex kind and then for the fragment kind:
// - the first map after initialization: whether the top byte of its offset is 0 for vertex and in
//   4 to 7 for fragment, and whether the offset is 4 KiB aligned
// - three maps A, B and C of 4 KiB, then B unmapped and a map D of 4 KiB at another range:
//   whether D equals B, whether D is 4 KiB past C, and whether D equals A
// - a map A of 4 KiB, unmapped, then a map B of 8 KiB: whether B equals A and whether B is 4 KiB
//   past A
// - a map of 8 MiB and then one of 4 KiB: whether the top byte of the second offset is one more
//   than the top byte of the first
// Last, sceGxmCreateContext with a fragment USSE ring buffer in a block mapped as fragment USSE
// memory, once with the offset of that map and once with the offset of a vertex USSE map. A
// relation is 0 when a map it needs failed, the codes of the maps are printed before it.
//
// On a PS Vita TV on firmware 3.65 every map and unmap works, for both kinds. The top byte of the
// first offset is in its range and the offset is 4 KiB aligned. In the hole case D equals B, it is
// not 4 KiB past C and does not equal A. In the bigger case B of 8 KiB equals A and is not 4 KiB
// past A. In the wrap case the top byte of the second offset is one more than the first. The
// context with the offset of the fragment USSE map is created and destroyed. The one with the
// offset of a vertex USSE map fails with 0x805B0004 (SCE_GXM_ERROR_INVALID_POINTER).
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE

// The calls of one USSE kind
typedef struct Kind {
    const char *name;
    int (*map)(void *base, SceSize size, unsigned int *offset);
    int (*unmap)(void *base);
    unsigned int top_min, top_max; // Where the top byte of the first offset has to be
} Kind;

static const Kind vertex = { "vertex", sceGxmMapVertexUsseMemory, sceGxmUnmapVertexUsseMemory, 0,
                             0 };
static const Kind fragment = { "fragment", sceGxmMapFragmentUsseMemory,
                               sceGxmUnmapFragmentUsseMemory, 4, 7 };

static char host_mem[HOST_SIZE + 64] __attribute__((aligned(16)));

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

// Gives every USSE heap a new start. The code is 0 when both calls worked.
static int restart(void) {
    const int terminated = sceGxmTerminate();
    const int initialized = initialize();
    return terminated ? terminated : initialized;
}

static char *alloc(SceUID *uid, SceSize size) {
    *uid = sceKernelAllocMemBlock("gxm usse offset", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                  size, NULL);
    void *base = NULL;
    if (*uid >= 0)
        sceKernelGetMemBlockBase(*uid, &base);
    return base;
}

static void first_map(const Kind *kind) {
    PRINTF(restart(), "%s USSE: restart libgxm", kind->name);
    SceUID uid;
    char *const base = alloc(&uid, 64 * KIB);
    PRINTF(test_created(uid), "%s USSE: first map: allocate a block", kind->name);
    if (uid < 0)
        return;
    unsigned int offset = 0;
    const int mapped = kind->map(base, 4 * KIB, &offset);
    PRINTF(mapped, "%s USSE: first map", kind->name);
    PRINTF(mapped == 0 && (offset >> 24) >= kind->top_min && (offset >> 24) <= kind->top_max,
           "%s USSE: first map: the top byte is in its range", kind->name);
    PRINTF(mapped == 0 && (offset & (4 * KIB - 1)) == 0, "%s USSE: first map: 4 KiB aligned",
           kind->name);
    if (mapped == 0)
        PRINTF(kind->unmap(base), "%s USSE: first map: unmap it", kind->name);
    sceKernelFreeMemBlock(uid);
}

static void reuse_hole(const Kind *kind) {
    PRINTF(restart(), "%s USSE: restart libgxm", kind->name);
    SceUID uid;
    char *const base = alloc(&uid, 64 * KIB);
    PRINTF(test_created(uid), "%s USSE: hole: allocate a block", kind->name);
    if (uid < 0)
        return;
    unsigned int a = 0, b = 0, c = 0, d = 0;
    const int ra = kind->map(base, 4 * KIB, &a);
    const int rb = kind->map(base + 4 * KIB, 4 * KIB, &b);
    const int rc = kind->map(base + 8 * KIB, 4 * KIB, &c);
    PRINTF(ra, "%s USSE: hole: map A", kind->name);
    PRINTF(rb, "%s USSE: hole: map B", kind->name);
    PRINTF(rc, "%s USSE: hole: map C", kind->name);
    int rd = -1;
    if (rb == 0)
        PRINTF(kind->unmap(base + 4 * KIB), "%s USSE: hole: unmap B", kind->name);
    if (ra == 0 && rc == 0) {
        rd = kind->map(base + 12 * KIB, 4 * KIB, &d);
        PRINTF(rd, "%s USSE: hole: map D", kind->name);
    }
    const int all = ra == 0 && rb == 0 && rc == 0 && rd == 0;
    PRINTF(all && d == b, "%s USSE: hole: D equals B", kind->name);
    PRINTF(all && d == c + 4 * KIB, "%s USSE: hole: D is 4 KiB past C", kind->name);
    PRINTF(all && d == a, "%s USSE: hole: D equals A", kind->name);
    if (rd == 0)
        kind->unmap(base + 12 * KIB);
    if (rc == 0)
        kind->unmap(base + 8 * KIB);
    if (ra == 0)
        kind->unmap(base);
    sceKernelFreeMemBlock(uid);
}

static void remap_bigger(const Kind *kind) {
    PRINTF(restart(), "%s USSE: restart libgxm", kind->name);
    SceUID uid;
    char *const base = alloc(&uid, 64 * KIB);
    PRINTF(test_created(uid), "%s USSE: bigger: allocate a block", kind->name);
    if (uid < 0)
        return;
    unsigned int a = 0, b = 0;
    const int ra = kind->map(base, 4 * KIB, &a);
    PRINTF(ra, "%s USSE: bigger: map A", kind->name);
    int rb = -1;
    if (ra == 0) {
        PRINTF(kind->unmap(base), "%s USSE: bigger: unmap A", kind->name);
        rb = kind->map(base + 16 * KIB, 8 * KIB, &b);
        PRINTF(rb, "%s USSE: bigger: map B of 8 KiB", kind->name);
    }
    PRINTF(rb == 0 && b == a, "%s USSE: bigger: B equals A", kind->name);
    PRINTF(rb == 0 && b == a + 4 * KIB, "%s USSE: bigger: B is 4 KiB past A", kind->name);
    if (rb == 0)
        kind->unmap(base + 16 * KIB);
    sceKernelFreeMemBlock(uid);
}

static void wrap(const Kind *kind) {
    PRINTF(restart(), "%s USSE: restart libgxm", kind->name);
    SceUID uid;
    char *const base = alloc(&uid, 16 * MIB);
    PRINTF(test_created(uid), "%s USSE: wrap: allocate a block", kind->name);
    if (uid < 0)
        return;
    unsigned int a = 0, b = 0;
    const int ra = kind->map(base, 8 * MIB, &a);
    PRINTF(ra, "%s USSE: wrap: map 8 MiB", kind->name);
    int rb = -1;
    if (ra == 0) {
        rb = kind->map(base + 8 * MIB, 4 * KIB, &b);
        PRINTF(rb, "%s USSE: wrap: map 4 KiB after it", kind->name);
    }
    PRINTF(rb == 0 && (b >> 24) == (a >> 24) + 1, "%s USSE: wrap: the top byte is one more",
           kind->name);
    if (rb == 0)
        kind->unmap(base + 8 * MIB);
    if (ra == 0)
        kind->unmap(base);
    sceKernelFreeMemBlock(uid);
}

// Makes a context whose fragment USSE ring buffer is in usse_base with the given offset
static void create_context(const char *label, char *rings_base, char *usse_base,
                           unsigned int usse_offset) {
    SceGxmContextParams params = { 0 };
    params.hostMem = host_mem;
    params.hostMemSize = HOST_SIZE;
    params.vdmRingBufferMem = rings_base;
    params.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    params.vertexRingBufferMem = rings_base + MIB;
    params.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    params.fragmentRingBufferMem = rings_base + 256 * KIB;
    params.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferMem = usse_base;
    params.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferOffset = usse_offset;
    SceGxmContext *context = NULL;
    const int created = sceGxmCreateContext(&params, &context);
    PRINTF(created, "%s", label);
    if (created == 0)
        PRINTF(sceGxmDestroyContext(context), "%s: destroy it", label);
}

static void contexts(void) {
    PRINT("context: restart libgxm", restart());
    SceUID rings, frag, vert;
    char *const rings_base = alloc(&rings, 4 * MIB);
    char *const frag_base = alloc(&frag, 64 * KIB);
    char *const vert_base = alloc(&vert, 64 * KIB);
    PRINT("context: allocate the blocks", test_created(rings) | test_created(frag) |
                                              test_created(vert));
    if (rings < 0 || frag < 0 || vert < 0) {
        sceKernelFreeMemBlock(rings);
        sceKernelFreeMemBlock(frag);
        sceKernelFreeMemBlock(vert);
        return;
    }
    unsigned int frag_offset = 0, vert_offset = 0;
    const int rr = sceGxmMapMemory(rings_base, 4 * MIB, SCE_GXM_MEMORY_ATTRIB_READ);
    const int rf = sceGxmMapFragmentUsseMemory(frag_base, 64 * KIB, &frag_offset);
    const int rv = sceGxmMapVertexUsseMemory(vert_base, 64 * KIB, &vert_offset);
    PRINT("context: map the ring buffers", rr);
    PRINT("context: map the fragment USSE block", rf);
    PRINT("context: map the vertex USSE block", rv);
    if (rr == 0 && rf == 0) {
        create_context("context: the offset of the fragment USSE map", rings_base, frag_base,
                       frag_offset);
        if (rv == 0)
            create_context("context: the offset of a vertex USSE map", rings_base, frag_base,
                           vert_offset);
    }
    if (rv == 0)
        sceGxmUnmapVertexUsseMemory(vert_base);
    if (rf == 0)
        sceGxmUnmapFragmentUsseMemory(frag_base);
    if (rr == 0)
        sceGxmUnmapMemory(rings_base);
    sceKernelFreeMemBlock(vert);
    sceKernelFreeMemBlock(frag);
    sceKernelFreeMemBlock(rings);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    const Kind *const kinds[] = { &vertex, &fragment };
    for (int i = 0; i < 2; i++) {
        first_map(kinds[i]);
        reuse_hole(kinds[i]);
        remap_bigger(kinds[i]);
        wrap(kinds[i]);
    }
    contexts();
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
