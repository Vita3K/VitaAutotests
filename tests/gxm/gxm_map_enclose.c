// sceGxmMapMemory ranges that touch an existing mapping in ways gxm_map_memory doesn't cover. The
// block is 1 MiB of USER_RW_UNCACHE with the 64 KiB at 256 KiB mapped first. The cases:
// - the whole block, which encloses the mapping
// - 96 KiB that start 64 KiB before the mapping and end 32 KiB into it
// - the 64 KiB right before the mapping and the 64 KiB right after it, which only touch it
// - the whole block again after the first mapping is gone
// Every map that works is unmapped again, and the first mapping is unmapped before the last case.
//
// On a PS Vita TV on firmware 3.65 the range that encloses the mapping and the one that ends inside
// it fail with 0x805B0017 (SCE_GXM_ERROR_DRIVER). The 64 KiB right before and right after it map,
// and the whole block maps once the first mapping is gone.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)

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

// Maps, prints the result and unmaps a mapping that worked, printing that too
static void map_once(const char *label, void *base, SceSize size) {
    const int mapped = sceGxmMapMemory(base, size, RW);
    PRINTF(mapped, "%s", label);
    if (mapped == 0)
        PRINTF(sceGxmUnmapMemory(base), "%s: unmap it", label);
}

int main(void) {
    PRINT("initialize libgxm", initialize());

    const SceUID block = sceKernelAllocMemBlock("gxm", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, MIB, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    char *const start = base;
    char *const mapping = start + 256 * KIB;

    PRINT("map the 64 KiB at 256 KiB", sceGxmMapMemory(mapping, 64 * KIB, RW));
    map_once("the whole block over it", start, MIB);
    map_once("96 KiB that end inside it", mapping - 64 * KIB, 96 * KIB);
    map_once("the 64 KiB right before it", mapping - 64 * KIB, 64 * KIB);
    map_once("the 64 KiB right after it", mapping + 64 * KIB, 64 * KIB);
    PRINT("unmap the 64 KiB at 256 KiB", sceGxmUnmapMemory(mapping));
    map_once("the whole block after the unmap", start, MIB);

    sceKernelFreeMemBlock(block);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
