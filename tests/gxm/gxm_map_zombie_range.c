// What a block that was freed while the GPU mapping held it gives once the mapping is gone. Each
// case has a fresh 64 KiB block of USER_RW with a name written at its base, mapped whole, then
// freed (the free fails with BLOCK_IN_USE and drops the UID), then unmapped. The block is now a
// zombie, and the case prints what its old range gives:
// - sceGxmMapMemory of the range, unmapped again if it worked
// - sceGxmMapVertexUsseMemory of the range, unmapped again if it worked
// - sceKernelFindMemBlockByAddr of the base
// - sceKernelCreateSema with the name pointer at the base, which reads the range in the kernel, and
//   the semaphore deleted again if it was created
// - a fresh 64 KiB block allocated while the zombie exists, then whether its base is the zombie's
//   base, 1 when it is and 0 when it isn't, and the block freed
// The cases map the block with sceGxmMapMemory and with sceGxmMapVertexUsseMemory.
//
// The find prints 0 when it gives the old UID, the error when it fails and 1 when it gives another
// UID.
//
// On a PS Vita TV on firmware 3.65 both cases give the same. Freeing the mapped block fails with
// 0x80024703 (SCE_KERNEL_ERROR_BLOCK_IN_USE) and the unmap after it works. Then the zombie's
// range maps again, with sceGxmMapMemory and as vertex USSE, and each of those maps unmaps. Finding
// its base gives 0x80024501 (SCE_KERNEL_ERROR_INVALID_UID), so the zombie is in no lookup. The
// semaphore named by its base is created and deleted, so the kernel still reads the range. A fresh
// 64 KiB block gets another base than the zombie's, and it frees normally.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define SIZE (64 * KIB)

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
static void map_range(const char *label, char *base, int vertex) {
    SceUInt32 offset = 0;
    const int mapped
        = vertex ? sceGxmMapVertexUsseMemory(base, SIZE, &offset) : sceGxmMapMemory(base, SIZE, RW);
    PRINTF(mapped, "%s", label);
    if (mapped == 0)
        PRINTF(vertex ? sceGxmUnmapVertexUsseMemory(base) : sceGxmUnmapMemory(base), "%s: unmap it",
               label);
}

static void zombie(const char *label, int vertex) {
    const SceUID uid
        = sceKernelAllocMemBlock("gxm zombie", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    PRINTF(test_created(uid), "%s: allocate the block", label);
    if (uid < 0) {
        PRINTF(-1, "%s: skipped, no block", label);
        return;
    }
    void *out = NULL;
    const int got_base = sceKernelGetMemBlockBase(uid, &out);
    PRINTF(got_base, "%s: its base", label);
    char *const base = out;
    if (got_base < 0 || !base) {
        PRINTF(-1, "%s: skipped, no base", label);
        sceKernelFreeMemBlock(uid);
        return;
    }
    sceClibStrncpy(base, "zombie", 16);

    SceUInt32 offset = 0;
    const int mapped
        = vertex ? sceGxmMapVertexUsseMemory(base, SIZE, &offset) : sceGxmMapMemory(base, SIZE, RW);
    PRINTF(mapped, "%s: map it", label);
    if (mapped < 0) {
        PRINTF(-1, "%s: skipped, no mapping", label);
        sceKernelFreeMemBlock(uid);
        return;
    }
    PRINTF(sceKernelFreeMemBlock(uid), "%s: free it", label);
    PRINTF(vertex ? sceGxmUnmapVertexUsseMemory(base) : sceGxmUnmapMemory(base), "%s: unmap it",
           label);

    char text[96];
    sceClibSnprintf(text, sizeof(text), "%s: zombie, map the range with sceGxmMapMemory", label);
    map_range(text, base, 0);
    sceClibSnprintf(text, sizeof(text), "%s: zombie, map the range as vertex USSE", label);
    map_range(text, base, 1);
    const SceUID found = sceKernelFindMemBlockByAddr(base, 0);
    PRINTF(found == uid ? 0 : found < 0 ? found : 1, "%s: zombie, find its base", label);

    const SceUID sema = sceKernelCreateSema(base, 0, 0, 1, NULL);
    PRINTF(test_created(sema), "%s: zombie, create a semaphore named by its base", label);
    if (sema >= 0)
        PRINTF(sceKernelDeleteSema(sema), "%s: zombie, delete the semaphore", label);

    const SceUID fresh
        = sceKernelAllocMemBlock("gxm fresh", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SIZE, NULL);
    PRINTF(test_created(fresh), "%s: zombie, allocate a fresh block", label);
    if (fresh >= 0) {
        void *fresh_base = NULL;
        const int got_fresh = sceKernelGetMemBlockBase(fresh, &fresh_base);
        PRINTF(got_fresh, "%s: zombie, its base", label);
        if (got_fresh == 0)
            PRINTF(fresh_base == base, "%s: zombie, the fresh block is at the zombie's base",
                   label);
        PRINTF(sceKernelFreeMemBlock(fresh), "%s: zombie, free the fresh block", label);
    }
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    zombie("mapped with sceGxmMapMemory", 0);
    zombie("mapped as vertex USSE", 1);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
