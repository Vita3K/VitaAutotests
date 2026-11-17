// When a USSE range is unmapped and then another one is mapped, which offset it gets. In
// gxm_usse_offset_bits a map right after unmapping a 4 KiB range or a range between two others got
// the offset that was freed, but in gxm_usse_map a range mapped again after its unmap, with
// another mapping still there, did not get its old offset. This test varies one thing at a time to
// find what makes the difference. Only relations are printed, never an offset. Each case starts
// with sceGxmTerminate and sceGxmInitialize, so every USSE heap is new, and uses fresh blocks that
// are unmapped and freed after it. It runs for the vertex kind and then for the fragment kind.
//
// Every case maps A and B of 4 KiB unless it says otherwise, unmaps A (and B if it says so), maps
// C, and prints whether C equals A, whether C equals B, whether C is exactly the size of A past A
// and whether C is exactly the size of B past B:
// - base: A and B in one block 64 KiB apart, then C at the range of A
// - warmup: the same, after three other 4 KiB ranges were mapped and unmapped in the heap, to see
//   whether it matters that the map is not the first one after sceGxmInitialize
// - separate blocks: the same, but B is in another block than A
// - another range: C is at another range of the block of A, with the same size as A
// - B unmapped too: the same as base, but B is unmapped before C is mapped
// - B first: B is mapped before A, so A is the last mapping when it is unmapped
// - C of 8 KiB: C is at the range of A but twice its size, so it doesn't fit in A's hole
// - A of 8 KiB: A is 8 KiB and C is 4 KiB at the range of A
// - B of 8 KiB: B is 8 KiB, A and C are 4 KiB
//
// On a PS Vita TV on firmware 3.65 the vertex and fragment kinds give the same relations. C gets
// A's offset only when B was unmapped too or when B was mapped first, so that A was the last range
// handed out. In every other case C gets the offset right after B, whatever the blocks, the range
// of C and the sizes of A, B and C, also after earlier maps and unmaps.
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

static const Kind vertex = { "vertex", sceGxmMapVertexUsseMemory, sceGxmUnmapVertexUsseMemory };
static const Kind fragment = { "fragment", sceGxmMapFragmentUsseMemory,
                               sceGxmUnmapFragmentUsseMemory };

// What differs between the cases, sizes are in KiB
typedef struct Case {
    const char *name;
    int warmup;         // Map and unmap three other ranges first
    int separate;       // B is in another block than A
    int same_range;     // C is at the range of A, else at another range of the block of A
    int keep_b;         // B stays mapped when C is mapped
    int b_first;        // B is mapped before A
    unsigned a, b, c;   // Sizes of A, B and C
} Case;

static const Case cases[] = {
    { "base", 0, 0, 1, 1, 0, 4, 4, 4 },
    { "warmup", 1, 0, 1, 1, 0, 4, 4, 4 },
    { "separate blocks", 0, 1, 1, 1, 0, 4, 4, 4 },
    { "another range", 0, 0, 0, 1, 0, 4, 4, 4 },
    { "B unmapped too", 0, 0, 1, 0, 0, 4, 4, 4 },
    { "B first", 0, 0, 1, 1, 1, 4, 4, 4 },
    { "C of 8 KiB", 0, 0, 1, 1, 0, 4, 4, 8 },
    { "A of 8 KiB", 0, 0, 1, 1, 0, 8, 4, 4 },
    { "B of 8 KiB", 0, 0, 1, 1, 0, 4, 8, 4 },
};

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
    *uid = sceKernelAllocMemBlock("gxm usse remap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                  size, NULL);
    void *base = NULL;
    if (*uid >= 0)
        sceKernelGetMemBlockBase(*uid, &base);
    return base;
}

// Maps three 4 KiB ranges and unmaps them in the order they were mapped
static void warm_up(const Kind *kind, const char *label) {
    SceUID uid;
    char *const base = alloc(&uid, 64 * KIB);
    PRINTF(test_created(uid), "%s USSE: %s: allocate the warmup block", kind->name, label);
    if (uid < 0)
        return;
    unsigned int offset = 0;
    int mapped[3];
    for (int i = 0; i < 3; i++)
        mapped[i] = kind->map(base + i * 8 * KIB, 4 * KIB, &offset);
    PRINTF(mapped[0] | mapped[1] | mapped[2], "%s USSE: %s: warmup maps", kind->name, label);
    for (int i = 0; i < 3; i++)
        if (mapped[i] == 0)
            kind->unmap(base + i * 8 * KIB);
    sceKernelFreeMemBlock(uid);
}

static void run_case(const Kind *kind, const Case *test) {
    PRINTF(restart(), "%s USSE: %s: restart libgxm", kind->name, test->name);
    if (test->warmup)
        warm_up(kind, test->name);

    SceUID uid_a, uid_b = -1;
    char *const base_a = alloc(&uid_a, 128 * KIB);
    PRINTF(test_created(uid_a), "%s USSE: %s: allocate the block of A", kind->name, test->name);
    char *base_b = base_a + 64 * KIB;
    if (test->separate) {
        base_b = alloc(&uid_b, 128 * KIB);
        PRINTF(test_created(uid_b), "%s USSE: %s: allocate the block of B", kind->name,
               test->name);
    }
    if (uid_a < 0 || (test->separate && uid_b < 0)) {
        sceKernelFreeMemBlock(uid_a);
        if (uid_b >= 0)
            sceKernelFreeMemBlock(uid_b);
        return;
    }

    unsigned int a = 0, b = 0, c = 0;
    int ra, rb;
    if (test->b_first) {
        rb = kind->map(base_b, test->b * KIB, &b);
        ra = kind->map(base_a, test->a * KIB, &a);
    } else {
        ra = kind->map(base_a, test->a * KIB, &a);
        rb = kind->map(base_b, test->b * KIB, &b);
    }
    PRINTF(ra, "%s USSE: %s: map A", kind->name, test->name);
    PRINTF(rb, "%s USSE: %s: map B", kind->name, test->name);

    int unmapped_a = -1, unmapped_b = 0, rc = -1;
    char *const base_c = test->same_range ? base_a : base_a + 32 * KIB;
    if (ra == 0 && rb == 0) {
        unmapped_a = kind->unmap(base_a);
        PRINTF(unmapped_a, "%s USSE: %s: unmap A", kind->name, test->name);
        if (!test->keep_b) {
            unmapped_b = kind->unmap(base_b);
            PRINTF(unmapped_b, "%s USSE: %s: unmap B", kind->name, test->name);
        }
        if (unmapped_a == 0 && unmapped_b == 0) {
            rc = kind->map(base_c, test->c * KIB, &c);
            PRINTF(rc, "%s USSE: %s: map C", kind->name, test->name);
        }
    }
    const int all = rc == 0;
    PRINTF(all && c == a, "%s USSE: %s: C equals A", kind->name, test->name);
    PRINTF(all && c == b, "%s USSE: %s: C equals B", kind->name, test->name);
    PRINTF(all && c == a + test->a * KIB, "%s USSE: %s: C is the size of A past A", kind->name,
           test->name);
    PRINTF(all && c == b + test->b * KIB, "%s USSE: %s: C is the size of B past B", kind->name,
           test->name);

    if (rc == 0)
        kind->unmap(base_c);
    if (rb == 0 && test->keep_b)
        kind->unmap(base_b);
    if (ra == 0 && unmapped_a != 0)
        kind->unmap(base_a);
    if (rb == 0 && !test->keep_b && unmapped_b != 0)
        kind->unmap(base_b);
    if (uid_b >= 0)
        sceKernelFreeMemBlock(uid_b);
    sceKernelFreeMemBlock(uid_a);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    const Kind *const kinds[] = { &vertex, &fragment };
    for (int i = 0; i < 2; i++)
        for (unsigned j = 0; j < sizeof(cases) / sizeof(cases[0]); j++)
            run_case(kinds[i], &cases[j]);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
