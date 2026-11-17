// The vertex and fragment USSE mappings that sceGxmTerminate drops. Two cases, each with fresh
// 64 KiB blocks of USER_RW, and nothing reads or writes them:
// - 4 KiB of one block mapped with sceGxmMapVertexUsseMemory and 4 KiB of another with
//   sceGxmMapFragmentUsseMemory, then sceGxmTerminate and sceGxmInitialize again. Then both
//   unmaps, both ranges mapped again and unmapped, and both blocks freed.
// - the same two mappings on two more blocks, left at the terminate, and the blocks freed right
//   after the terminate, before libgxm is initialized again, with no unmap at all.
// If a block can't be made or a map fails, the steps that need it print a fixed line instead. If
// libgxm is not up when a case starts, the case initializes it first and prints nothing for it.
//
// On a PS Vita TV on firmware 3.65 sceGxmTerminate and the new sceGxmInitialize work with both
// USSE mappings left, and they drop the mappings. The unmap of the vertex range and the unmap of
// the fragment range then both fail with 0x805B0017 (SCE_GXM_ERROR_DRIVER). Both ranges map again
// with 0, unmap with 0, and both blocks free with 0. In the second case the blocks free with 0
// right after the terminate, with no unmap at all. Every other line is 0.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)

#define SKIPPED -1

static int gxm_up;

static void display_callback(const void *data) {
    (void)data;
}

static int initialize(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    const int ret = sceGxmInitialize(&init);
    if (ret == 0)
        gxm_up = 1;
    return ret;
}

static int terminate(void) {
    const int ret = sceGxmTerminate();
    if (ret == 0)
        gxm_up = 0;
    return ret;
}

// Brings libgxm up for a case if an earlier step left it down
static void ensure_up(void) {
    if (!gxm_up)
        initialize();
}

static SceUID make(const char *label, char **base) {
    const SceUID uid
        = sceKernelAllocMemBlock("gxm usse terminate", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    PRINT(label, test_created(uid));
    *base = NULL;
    if (uid < 0)
        return -1;
    void *out = NULL;
    if (sceKernelGetMemBlockBase(uid, &out) < 0 || !out) {
        sceKernelFreeMemBlock(uid);
        return -1;
    }
    *base = out;
    return uid;
}

// Maps 4 KiB of the base, or gives SKIPPED if there is no block
static int map(int fragment, char *base) {
    if (!base)
        return SKIPPED;
    SceUInt32 offset = 0;
    return fragment ? sceGxmMapFragmentUsseMemory(base, 4 * KIB, &offset)
                    : sceGxmMapVertexUsseMemory(base, 4 * KIB, &offset);
}

static int unmap(int fragment, char *base) {
    if (!base)
        return SKIPPED;
    return fragment ? sceGxmUnmapFragmentUsseMemory(base) : sceGxmUnmapVertexUsseMemory(base);
}

// Frees the block, and if that fails tries again after unmapping both USSE ranges so it doesn't leak
static int free_block(SceUID uid, char *base) {
    if (uid < 0)
        return SKIPPED;
    const int ret = sceKernelFreeMemBlock(uid);
    if (ret < 0) {
        sceGxmUnmapVertexUsseMemory(base);
        sceGxmUnmapFragmentUsseMemory(base);
        sceKernelFreeMemBlock(uid);
    }
    return ret;
}

static void dropped_then_unmapped(void) {
    ensure_up();
    char *vertex_base, *fragment_base;
    const SceUID vertex_block = make("unmapped: allocate the vertex block", &vertex_base);
    const SceUID fragment_block = make("unmapped: allocate the fragment block", &fragment_base);
    const int vertex_mapped = map(0, vertex_base);
    PRINT("unmapped: map vertex USSE", vertex_mapped);
    const int fragment_mapped = map(1, fragment_base);
    PRINT("unmapped: map fragment USSE", fragment_mapped);
    PRINT("unmapped: terminate libgxm with them mapped", terminate());
    PRINT("unmapped: initialize libgxm again", initialize());

    PRINT("unmapped: unmap vertex USSE", vertex_mapped == 0 ? unmap(0, vertex_base) : SKIPPED);
    PRINT("unmapped: unmap fragment USSE", fragment_mapped == 0 ? unmap(1, fragment_base) : SKIPPED);

    const int vertex_again = map(0, vertex_base);
    PRINT("unmapped: map the vertex USSE range again", vertex_again);
    const int fragment_again = map(1, fragment_base);
    PRINT("unmapped: map the fragment USSE range again", fragment_again);
    PRINT("unmapped: unmap vertex USSE again", vertex_again == 0 ? unmap(0, vertex_base) : SKIPPED);
    PRINT("unmapped: unmap fragment USSE again",
          fragment_again == 0 ? unmap(1, fragment_base) : SKIPPED);

    PRINT("unmapped: free the vertex block", free_block(vertex_block, vertex_base));
    PRINT("unmapped: free the fragment block", free_block(fragment_block, fragment_base));
}

static void dropped_then_freed(void) {
    ensure_up();
    char *vertex_base, *fragment_base;
    const SceUID vertex_block = make("freed: allocate the vertex block", &vertex_base);
    const SceUID fragment_block = make("freed: allocate the fragment block", &fragment_base);
    PRINT("freed: map vertex USSE", map(0, vertex_base));
    PRINT("freed: map fragment USSE", map(1, fragment_base));
    PRINT("freed: terminate libgxm with them mapped", terminate());
    // Freed before libgxm is initialized again, so only the terminate can have dropped the pins
    PRINT("freed: free the vertex block", free_block(vertex_block, vertex_base));
    PRINT("freed: free the fragment block", free_block(fragment_block, fragment_base));
    PRINT("freed: initialize libgxm again", initialize());
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    dropped_then_unmapped();
    dropped_then_freed();
    PRINT("terminate libgxm", terminate());
    test_exit();
    return 0;
}
