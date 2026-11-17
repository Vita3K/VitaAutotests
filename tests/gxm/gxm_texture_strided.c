// What sceGxmTextureInitLinearStrided writes into the four words of a texture for strides that need
// 13, 14 and 15 bits once stored. The texture is 8 by 2 U8U8U8U8_ABGR at the base of a 1 MiB
// USER_RW_UNCACHE block mapped with READ, and no scene starts, so the GPU never reads it. The byte
// strides are 6, 4, 32764, 32768, 32772, 65536, 98304, 131072 and 131076. Each init prints its
// result, and one that works prints sceGxmTextureGetStride, sceGxmTextureGetUAddrMode,
// sceGxmTextureGetVAddrMode, sceGxmTextureValidate, word 0 bits 2:0, the stored stride field read
// as stride / 4 - 1 with bits 12:3 in word 0 bits 26:17, bits 2:0 in word 0 bits 11:9 and bits
// 14:13 in word 0 bits 2:1, and words 0, 1 and 3 in full. Word 2 holds the data address, so only
// its bits 1:0 are printed.
//
// On a PS Vita TV on firmware 3.65 every stride from 4 to 131072 works and keeps its full value.
// sceGxmTextureGetStride gives the stride back, and the stored field keeps bits 14:13 in word 0
// bits 2:1, so word 0 is 0x00000092, 0x07FE0E92, 0x07FE0E94 and 0x07FE0E96 for 32772, 65536,
// 98304 and 131072. A stride of 6 fails with 0x805B0005 (SCE_GXM_ERROR_INVALID_ALIGNMENT) and
// 131076 with 0x805B0003 (SCE_GXM_ERROR_INVALID_VALUE). Both address modes are 2 (clamp) and every
// texture that was made validates.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB 0x100000

#define BLOCK_SIZE MIB

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

// The stride field of a strided texture's word 0, as stride / 4 - 1
static unsigned int stride_field(unsigned int word0) {
    return ((word0 >> 9) & 7) | (((word0 >> 17) & 0x3ff) << 3) | (((word0 >> 1) & 3) << 13);
}

static void strided(const void *data, unsigned int stride) {
    SceGxmTexture texture;
    sceClibMemset(&texture, 0, sizeof(texture));
    const int got = sceGxmTextureInitLinearStrided(&texture, data,
                                                   SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 8, 2,
                                                   stride);
    PRINTF(got, "stride %u: init", stride);
    if (got != 0)
        return;
    const unsigned int *const words = (const unsigned int *)&texture;
    PRINTF(sceGxmTextureGetStride(&texture), "stride %u: get stride", stride);
    PRINTF(sceGxmTextureGetUAddrMode(&texture), "stride %u: U address mode", stride);
    PRINTF(sceGxmTextureGetVAddrMode(&texture), "stride %u: V address mode", stride);
    PRINTF(sceGxmTextureValidate(&texture), "stride %u: validate", stride);
    PRINTF(words[0] & 7, "stride %u: word 0 bits 2:0", stride);
    PRINTF(stride_field(words[0]), "stride %u: stride field", stride);
    PRINTF(words[0], "stride %u: word 0", stride);
    PRINTF(words[1], "stride %u: word 1", stride);
    PRINTF(words[2] & 3, "stride %u: word 2 bits 1:0", stride);
    PRINTF(words[3], "stride %u: word 3", stride);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    const SceUID block = sceKernelAllocMemBlock("gxm strided", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                                BLOCK_SIZE, NULL);
    PRINT("allocate the block", test_created(block));
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    PRINT("map the block", sceGxmMapMemory(base, BLOCK_SIZE, SCE_GXM_MEMORY_ATTRIB_READ));

    static const unsigned int strides[] = { 6,     4,     32764,  32768, 32772,
                                            65536, 98304, 131072, 131076 };
    for (unsigned int i = 0; i < sizeof(strides) / sizeof(strides[0]); i++)
        strided(base, strides[i]);

    PRINT("unmap the block", sceGxmUnmapMemory(base));
    sceKernelFreeMemBlock(block);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
