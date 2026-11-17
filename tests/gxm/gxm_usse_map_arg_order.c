// Which argument sceGxmMapVertexUsseMemory and sceGxmMapFragmentUsseMemory check first when more
// than one is wrong. No GPU work runs. First, before sceGxmInitialize, for each kind a map with a
// NULL base and a NULL offset, a map with a NULL base and a valid offset, and an unmap of a NULL
// base, then sceGxmUnmapMemory of a NULL base, to see whether the initialization check comes first.
// Then, with libgxm initialized, for each kind:
// - a NULL base and a NULL offset, with sizes 4 KiB, 9 MiB and 0
// - a NULL base and a valid offset, with sizes 9 MiB and 0
// - a valid base and a NULL offset, with sizes 9 MiB and 0, and with a base that isn't 4 KiB
//   aligned
// - a valid base and offset at a base that isn't 4 KiB aligned, with sizes 9 MiB and 0
// - a valid base and offset at an aligned base, with size 9 MiB, and with size 4 KiB as a control
// Each case with a valid base uses a fresh 16 MiB block that is freed after it. Every map that
// works is unmapped again. Last, the unmap of a NULL base for each kind with libgxm initialized and
// after sceGxmTerminate.
//
// On a PS Vita TV on firmware 3.65 every call before sceGxmInitialize and after sceGxmTerminate
// gives 0x805B0000 (SCE_GXM_ERROR_UNINITIALIZED), also with a NULL base. With libgxm initialized,
// a NULL base gives 0x805B0004 (SCE_GXM_ERROR_INVALID_POINTER) whatever the offset and the size
// are, and so does a NULL offset with a valid base, also with size 9 MiB, size 0 or an unaligned
// base. With a valid base and offset, an unaligned base with size 9 MiB gives 0x805B0003
// (SCE_GXM_ERROR_INVALID_VALUE), the same as an aligned base with size 9 MiB. An unaligned base
// with size 0 gives 0x805B0017 (SCE_GXM_ERROR_DRIVER). An aligned base with 4 KiB maps and
// unmaps. The vertex and fragment calls give the same codes in every case, and the unmap of a
// NULL base gives 0x805B0004 while libgxm is initialized.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

// The calls of one USSE kind
typedef struct Kind {
    const char *name;
    int (*map)(void *base, SceSize size, unsigned int *offset);
    int (*unmap)(void *base);
} Kind;

static const Kind vertex = { "vertex", sceGxmMapVertexUsseMemory, sceGxmUnmapVertexUsseMemory };
static const Kind fragment = { "fragment", sceGxmMapFragmentUsseMemory,
                               sceGxmUnmapFragmentUsseMemory };

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
static void map_once(const Kind *kind, const char *label, void *base, SceSize size,
                     unsigned int *offset) {
    const int mapped = kind->map(base, size, offset);
    PRINTF(mapped, "%s USSE: %s", kind->name, label);
    if (mapped == 0)
        PRINTF(kind->unmap(base), "%s USSE: %s: unmap it", kind->name, label);
}

static void before_initialize(const Kind *kind) {
    unsigned int offset = 0;
    PRINTF(kind->map(NULL, 4 * KIB, NULL), "%s USSE: before initialize: NULL base and offset",
           kind->name);
    PRINTF(kind->map(NULL, 4 * KIB, &offset), "%s USSE: before initialize: NULL base", kind->name);
    PRINTF(kind->unmap(NULL), "%s USSE: before initialize: unmap a NULL base", kind->name);
}

// Maps base_delta bytes into a fresh 16 MiB block, with or without an offset, and frees the block
static void map_fresh(const Kind *kind, const char *label, SceSize base_delta, SceSize size,
                      int with_offset) {
    SceUID uid = sceKernelAllocMemBlock("gxm usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                        16 * MIB, NULL);
    void *base = NULL;
    if (uid >= 0)
        sceKernelGetMemBlockBase(uid, &base);
    if (base == NULL) {
        PRINTF(uid < 0 ? uid : 1, "%s USSE: %s: no block", kind->name, label);
    } else {
        unsigned int offset = 0;
        map_once(kind, label, (char *)base + base_delta, size, with_offset ? &offset : NULL);
    }
    if (uid >= 0)
        sceKernelFreeMemBlock(uid);
}

static void arg_order(const Kind *kind) {
    unsigned int offset = 0;
    map_once(kind, "NULL base and offset, 4 KiB", NULL, 4 * KIB, NULL);
    map_once(kind, "NULL base and offset, 9 MiB", NULL, 9 * MIB, NULL);
    map_once(kind, "NULL base and offset, size 0", NULL, 0, NULL);
    map_once(kind, "NULL base, valid offset, 9 MiB", NULL, 9 * MIB, &offset);
    map_once(kind, "NULL base, valid offset, size 0", NULL, 0, &offset);
    map_fresh(kind, "valid base, NULL offset, 9 MiB", 0, 9 * MIB, 0);
    map_fresh(kind, "valid base, NULL offset, size 0", 0, 0, 0);
    map_fresh(kind, "valid base, NULL offset, unaligned base", 2 * KIB, 4 * KIB, 0);
    map_fresh(kind, "unaligned base, 9 MiB", 2 * KIB, 9 * MIB, 1);
    map_fresh(kind, "unaligned base, size 0", 2 * KIB, 0, 1);
    map_fresh(kind, "aligned base, 9 MiB", 0, 9 * MIB, 1);
    map_fresh(kind, "aligned base, 4 KiB", 0, 4 * KIB, 1);
}

int main(void) {
    before_initialize(&vertex);
    before_initialize(&fragment);
    PRINT("unmap a NULL base with sceGxmUnmapMemory before initialize", sceGxmUnmapMemory(NULL));

    PRINT("initialize libgxm", initialize());
    arg_order(&vertex);
    arg_order(&fragment);
    PRINT("vertex USSE: unmap a NULL base", sceGxmUnmapVertexUsseMemory(NULL));
    PRINT("fragment USSE: unmap a NULL base", sceGxmUnmapFragmentUsseMemory(NULL));
    PRINT("terminate libgxm", sceGxmTerminate());
    PRINT("vertex USSE: after terminate: unmap a NULL base", sceGxmUnmapVertexUsseMemory(NULL));
    PRINT("fragment USSE: after terminate: unmap a NULL base",
          sceGxmUnmapFragmentUsseMemory(NULL));
    test_exit();
    return 0;
}
