// More attribute bits for sceGxmMapMemory, next to gxm_map_attrib_bits. Each case maps a fresh
// 64 KiB USER_RW block, prints the result, and unmaps the mapping if it worked, printing that too.
// The cases:
// - READ (0x1) combined with each single bit 0x4, 0x20, 0x40 and 0x80
// - READ combined with each single bit from 0x400 up to 0x40000000
// - WRITE (0x2) combined with 0x8, and WRITE combined with 0x10, without READ
// - READ and WRITE with 0x200, 0x80000000 and 0x100 all together
// Every case is made with libgxm initialized, which is done at the start and undone at the end.
//
// On a PS Vita TV on firmware 3.65 READ with any of 0x4, 0x20, 0x40 and 0x80 fails with
// 0x805B0017 (SCE_GXM_ERROR_DRIVER), and so do WRITE with 0x8 and WRITE with 0x10. READ with each
// bit from 0x400 to 0x40000000 maps and unmaps with 0, and so does 0x80000303, every accepted bit
// together.
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

// Maps a fresh USER_RW block with the attributes, prints the result and unmaps a mapping that worked
static void attrib_case(SceUInt32 attribs) {
    char label[64];
    sceClibSnprintf(label, sizeof(label), "a USER_RW block, attributes 0x%X", attribs);

    const SceUID uid = sceKernelAllocMemBlock("gxm", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 64 * KIB, NULL);
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

    static const SceUInt32 low_bits[] = { 0x4, 0x20, 0x40, 0x80 };
    for (unsigned int i = 0; i < sizeof(low_bits) / sizeof(low_bits[0]); i++)
        attrib_case(0x1 | low_bits[i]);

    for (SceUInt32 bit = 0x400; bit <= 0x40000000; bit <<= 1)
        attrib_case(0x1 | bit);

    attrib_case(0x2 | 0x8);
    attrib_case(0x2 | 0x10);
    attrib_case(0x3 | 0x200 | 0x80000000 | 0x100);

    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
