// Which error sceGxmColorSurfaceInit gives for each kind of bad argument, and which check wins when
// a call has two. The valid surface is 64 by 64 pixels, linear, U8U8U8U8_ABGR, with a stride of 64
// and the 32 bit output register size, at the base of a 4 MiB USER_RW_UNCACHE block mapped with
// READ and WRITE. No scene starts, so the GPU never touches the memory. Each case changes the valid
// surface as it says and prints the result. The cases:
// - the valid surface, a NULL surface, and the color formats 0x70000000, which is no format,
//   U8S8S8U8_ABGR, F16F16F16F16_ABGR with the 32 bit and the 64 bit register size, and
//   U8U8U8U8_ABGR with the 64 bit size
// - a stride of 65, of 62 for a width of 64, of 63 for a width of 64, data 2 bytes in, and data 2
//   bytes in with a stride of 65
// - a scale mode of 2, MSAA downscale on U8U8U8U8_ABGR and on F32_R, and a surface type of
//   0x01000000
// - swizzled surfaces of 64 by 64, 16 by 16, 8 by 8, 48 by 64 and 64 by 48
// - tiled surfaces of 64 by 64, 32 by 32, 48 by 48, 16 by 16, 31 by 32 and 32 by 31
// - linear U8U8U8_BGR of 60 by 60 with a stride of 64, which is the width rounded up to 8, and of
//   62, and linear U8U8U8U8_ABGR with a stride of 32768 and 32770 pixels
// Then the NULL surface last but one, and NULL data last.
//
// On a PS Vita TV on firmware 3.65 these fail with 0x805B0003 (SCE_GXM_ERROR_INVALID_VALUE): format
// 0x70000000, U8S8S8U8_ABGR, a stride below the width, data that isn't 4 byte aligned, a scale mode
// of 2, a surface type of 0x01000000, a swizzled size that isn't a power of two of at least 16, a
// tiled size below 32, a U8U8U8_BGR stride below the width rounded up to 8, and a stride above
// 32768 pixels. An odd stride fails with 0x805B0005 (SCE_GXM_ERROR_INVALID_ALIGNMENT) and wins over
// the other two stride and data checks. F16F16F16F16_ABGR with the 32 bit register size and MSAA
// downscale on F32_R fail with 0x805B0009 (SCE_GXM_ERROR_UNSUPPORTED). A NULL surface fails with
// 0x805B0004 (SCE_GXM_ERROR_INVALID_POINTER), and NULL data works.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define MIB 0x100000

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define BLOCK_SIZE (4 * MIB)
#define FORMAT SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR
#define LINEAR SCE_GXM_COLOR_SURFACE_LINEAR
#define TILED SCE_GXM_COLOR_SURFACE_TILED
#define SWIZZLED SCE_GXM_COLOR_SURFACE_SWIZZLED
#define SCALE_NONE SCE_GXM_COLOR_SURFACE_SCALE_NONE
#define SIZE_32 SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT
#define SIZE_64 SCE_GXM_OUTPUT_REGISTER_SIZE_64BIT

static char *mapped;

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

static void init(const char *what, SceGxmColorFormat format, SceGxmColorSurfaceType type,
                 SceGxmColorSurfaceScaleMode scale, SceGxmOutputRegisterSize size,
                 unsigned int width, unsigned int height, unsigned int stride, void *data) {
    SceGxmColorSurface surface;
    sceClibMemset(&surface, 0, sizeof(surface));
    PRINT(what, sceGxmColorSurfaceInit(&surface, format, type, scale, size, width, height, stride,
                                       data));
}

// A surface of the valid format, scale mode and register size
static void sized(const char *what, SceGxmColorSurfaceType type, unsigned int width,
                  unsigned int height, unsigned int stride) {
    init(what, FORMAT, type, SCALE_NONE, SIZE_32, width, height, stride, mapped);
}

static void formats(void) {
    sized("the valid surface", LINEAR, 64, 64, 64);
    init("format 0x70000000", (SceGxmColorFormat)0x70000000, LINEAR, SCALE_NONE, SIZE_32, 64, 64,
         64, mapped);
    init("U8S8S8U8_ABGR", SCE_GXM_COLOR_FORMAT_U8S8S8U8_ABGR, LINEAR, SCALE_NONE, SIZE_32, 64, 64,
         64, mapped);
    init("F16F16F16F16_ABGR with the 32 bit size", SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR, LINEAR,
         SCALE_NONE, SIZE_32, 64, 64, 64, mapped);
    init("F16F16F16F16_ABGR with the 64 bit size", SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR, LINEAR,
         SCALE_NONE, SIZE_64, 64, 64, 64, mapped);
    init("U8U8U8U8_ABGR with the 64 bit size", FORMAT, LINEAR, SCALE_NONE, SIZE_64, 64, 64, 64,
         mapped);
}

static void strides(void) {
    sized("a stride of 65", LINEAR, 64, 64, 65);
    sized("a stride of 62 for a width of 64", LINEAR, 64, 64, 62);
    sized("a stride of 63 for a width of 64", LINEAR, 64, 64, 63);
    init("data 2 bytes in", FORMAT, LINEAR, SCALE_NONE, SIZE_32, 64, 64, 64, mapped + 2);
    init("data 2 bytes in with a stride of 65", FORMAT, LINEAR, SCALE_NONE, SIZE_32, 64, 64, 65,
         mapped + 2);
}

static void modes(void) {
    init("a scale mode of 2", FORMAT, LINEAR, (SceGxmColorSurfaceScaleMode)2, SIZE_32, 64, 64, 64,
         mapped);
    init("MSAA downscale on U8U8U8U8_ABGR", FORMAT, LINEAR,
         SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE, SIZE_32, 64, 64, 64, mapped);
    init("MSAA downscale on F32_R", SCE_GXM_COLOR_FORMAT_F32_R, LINEAR,
         SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE, SIZE_32, 64, 64, 64, mapped);
    sized("a surface type of 0x01000000", (SceGxmColorSurfaceType)0x01000000, 64, 64, 64);
}

static void layouts(void) {
    sized("swizzled 64 by 64", SWIZZLED, 64, 64, 64);
    sized("swizzled 16 by 16", SWIZZLED, 16, 16, 16);
    sized("swizzled 8 by 8", SWIZZLED, 8, 8, 8);
    sized("swizzled 48 by 64", SWIZZLED, 48, 64, 48);
    sized("swizzled 64 by 48", SWIZZLED, 64, 48, 64);
    sized("tiled 64 by 64", TILED, 64, 64, 64);
    sized("tiled 32 by 32", TILED, 32, 32, 32);
    sized("tiled 48 by 48", TILED, 48, 48, 48);
    sized("tiled 16 by 16", TILED, 16, 16, 16);
    sized("tiled 31 by 32", TILED, 31, 32, 32);
    sized("tiled 32 by 31", TILED, 32, 31, 32);
}

static void linear_strides(void) {
    init("U8U8U8_BGR 60 by 60 with a stride of 64", SCE_GXM_COLOR_FORMAT_U8U8U8_BGR, LINEAR,
         SCALE_NONE, SIZE_32, 60, 60, 64, mapped);
    init("U8U8U8_BGR 60 by 60 with a stride of 62", SCE_GXM_COLOR_FORMAT_U8U8U8_BGR, LINEAR,
         SCALE_NONE, SIZE_32, 60, 60, 62, mapped);
    sized("a stride of 32768 pixels", LINEAR, 64, 8, 32768);
    sized("a stride of 32770 pixels", LINEAR, 64, 8, 32770);
}

int main(void) {
    PRINT("initialize libgxm", initialize());
    const SceUID block = sceKernelAllocMemBlock("gxm color surface",
                                                SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                                BLOCK_SIZE, NULL);
    PRINT("allocate the block", test_created(block));
    sceKernelGetMemBlockBase(block, (void **)&mapped);
    PRINT("map the block", sceGxmMapMemory(mapped, BLOCK_SIZE, RW));

    formats();
    strides();
    modes();
    layouts();
    linear_strides();

    // NULL pointers go last, so an implementation that crashes on them loses nothing else
    PRINT("a NULL surface", sceGxmColorSurfaceInit(NULL, FORMAT, LINEAR, SCALE_NONE, SIZE_32, 64,
                                                   64, 64, mapped));
    init("NULL data", FORMAT, LINEAR, SCALE_NONE, SIZE_32, 64, 64, 64, NULL);

    PRINT("unmap the block", sceGxmUnmapMemory(mapped));
    sceKernelFreeMemBlock(block);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
