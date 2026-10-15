// How each texture format and swizzle decodes: an 8x8 linear texture of bit patterns in each
// format, drawn 8 times larger with point filtering, so each texel covers 8x8 pixels of the capture.
// The F16 formats get halves from a table instead, since most bit patterns aren't useful ones.
#include <psp2/kernel/clib.h>

#include "capture.h"
#include "render.h"
#include "test.h"

#define T(name) { SCE_GXM_TEXTURE_FORMAT_##name, #name }

static const struct {
    SceGxmTextureFormat format;
    const char *name;
} formats[] = {
    T(U8U8U8U8_ABGR), T(U8U8U8U8_ARGB), T(U8U8U8U8_RGBA), T(U8U8U8U8_BGRA), T(X8U8U8U8_1BGR), T(X8U8U8U8_1RGB),
    T(U4U4U4U4_ABGR), T(U4U4U4U4_ARGB), T(U4U4U4U4_RGBA), T(U4U4U4U4_BGRA), T(X4U4U4U4_1BGR), T(U4U4U4X4_RGB1),
    T(U1U5U5U5_ABGR), T(U1U5U5U5_ARGB), T(U5U5U5U1_RGBA), T(U5U5U5U1_BGRA), T(X1U5U5U5_1BGR), T(U5U5U5X1_RGB1),
    T(U5U6U5_BGR),    T(U5U6U5_RGB),    T(U8U3U3U2_ARGB), T(U8_R),          T(U8_000R),      T(U8_111R),
    T(U8_RRRR),       T(U8_0RRR),       T(U8_1RRR),       T(U8_R000),       T(U8_R111),      T(S8_000R),
    T(S8_RRRR),       T(U8U8_GR),       T(U8U8_00GR),     T(U8U8_GRRR),     T(U8U8_RGGG),    T(U8U8_GRGR),
    T(U8U8_00RG),     T(S8S8_00GR),     T(U16_000R),      T(U16_RRRR),      T(S16_000R),     T(F16_000R),
    T(F16_RRRR),      T(F16_R111),
};

#define SIZE 8
// The bits of a format that hold its base format
#define BASE_FORMAT_MASK 0x9F000000u
#define CELL 80
#define COLUMNS (RENDER_WIDTH / CELL)

static int bytes_per_texel(SceGxmTextureFormat format) {
    switch ((unsigned int)format & BASE_FORMAT_MASK) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8:
        return 1;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8:
        return 4;
    default:
        return 2;
    }
}

// Halves from -2 to 2, with 0, 1, a tiny one and their negatives
static const unsigned short halves[16] = { 0x0000, 0x3C00, 0x3800, 0x3400, 0x3A00, 0x3BFF, 0x0001, 0x4000,
                                           0x8000, 0xBC00, 0xB800, 0x2E66, 0x3555, 0x3E00, 0xC000, 0x1400 };

static int is_half(SceGxmTextureFormat format) {
    return ((unsigned int)format & BASE_FORMAT_MASK) == SCE_GXM_TEXTURE_BASE_FORMAT_F16;
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *textured = render_texture_program(NULL);

    const int count = sizeof(formats) / sizeof(formats[0]);
    static SceGxmTexture textures[sizeof(formats) / sizeof(formats[0])];
    for (int i = 0; i < count; i++) {
        const int bpp = bytes_per_texel(formats[i].format);
        unsigned char *data = render_texture_memory(SIZE * SIZE * bpp);
        if (data == NULL)
            break;
        for (int t = 0; t < SIZE * SIZE; t++) {
            if (is_half(formats[i].format)) {
                const unsigned short half = halves[(t + t / SIZE) % 16];
                sceClibMemcpy(data + t * 2, &half, 2);
                continue;
            }
            // Every value of a channel turns up somewhere, whatever its size
            for (int b = 0; b < bpp; b++)
                data[t * bpp + b] = (t * 37 + b * 101 + 13) & 0xFF;
        }
        const int result = sceGxmTextureInitLinear(&textures[i], data, formats[i].format, SIZE, SIZE, 0);
        PRINTF(result, "set up a %s texture", formats[i].name);
        sceGxmTextureSetMinFilter(&textures[i], SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetMagFilter(&textures[i], SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetUAddrMode(&textures[i], SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&textures[i], SCE_GXM_TEXTURE_ADDR_CLAMP);
    }

    PRINT("begin a frame", render_begin());
    render_clear(0xFF404040);
    for (int i = 0; i < count; i++) {
        const float x = (i % COLUMNS) * CELL + 8, y = (i / COLUMNS) * CELL + 8;
        render_textured_rect(textured, &textures[i], x, y, x + SIZE * 8, y + SIZE * 8, 0, 0, 1, 1);
    }
    PRINT("draw each format", render_end());
    test_capture_display("formats");

    test_exit();
    return 0;
}
