// Which attribute bits sceGxmMapMemory lets through. Each case maps a fresh 64 KiB block, prints
// the result, and unmaps the mapping if it worked, printing that too. The cases:
// - a USER_RW block with the attribute values 0x100, 0x101, 0x102 and 0x103
// - a USER_RW block with READ combined with 0x200, 0x8, 0x10 and 0x80000000
// - a read only block with 0x101 and with 0x102, to see if bit 0x100 hides WRITE
// Every case is made with libgxm initialized, which is done at the start and undone at the end.
//
// On a PS Vita TV on firmware 3.65 the attribute value 0x100 alone fails with 0x805B0017
// (SCE_GXM_ERROR_DRIVER), while 0x101, 0x102 and 0x103 map and unmap with 0. READ combined with
// 0x200 or with 0x80000000 maps and unmaps with 0. READ combined with 0x8 or with 0x10 fails with
// 0x805B0017. On the read only block 0x101 maps and unmaps with 0, and 0x102 fails with
// 0x805B0017.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400

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

// Maps a fresh block with the attributes, prints the result and unmaps a mapping that worked
static void attrib_case(const char *kind, SceUInt32 type, SceUInt32 attribs) {
    char label[64];
    sceClibSnprintf(label, sizeof(label), "%s, attributes 0x%X", kind, attribs);

    const SceUID uid = sceKernelAllocMemBlock("gxm", type, 64 * KIB, NULL);
    void *base = NULL;
    if (uid < 0 || sceKernelGetMemBlockBase(uid, &base) < 0) {
        PRINTF(-1, "%s: no block", label);
        if (uid >= 0)
            sceKernelFreeMemBlock(uid);
        return;
    }

    const int mapped = sceGxmMapMemory(base, 64 * KIB, attribs);
    PRINTF(mapped, "%s", label);
    if (mapped == 0)
        PRINTF(sceGxmUnmapMemory(base), "%s: unmap it", label);
    sceKernelFreeMemBlock(uid);
}

int main(void) {
    PRINT("initialize libgxm", initialize());

    static const SceUInt32 bit_100[] = { 0x100, 0x101, 0x102, 0x103 };
    for (unsigned int i = 0; i < sizeof(bit_100) / sizeof(bit_100[0]); i++)
        attrib_case("a USER_RW block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, bit_100[i]);

    static const SceUInt32 other_bits[] = { 0x200 | 1, 0x8 | 1, 0x10 | 1, 0x80000000 | 1 };
    for (unsigned int i = 0; i < sizeof(other_bits) / sizeof(other_bits[0]); i++)
        attrib_case("a USER_RW block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, other_bits[i]);

    attrib_case("a read only block", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 0x101);
    attrib_case("a read only block", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R, 0x102);

    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
