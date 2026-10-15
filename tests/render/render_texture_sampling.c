// Texture sampling: point and linear filtering of an 8x8 texture drawn large, each address mode with
// coordinates from -1 to 2, and a 64x64 checkerboard drawn smaller than it is.
#include "capture.h"
#include "render.h"
#include "test.h"

#define A(name) SCE_GXM_TEXTURE_ADDR_##name
#define POINT SCE_GXM_TEXTURE_FILTER_POINT
#define LINEAR SCE_GXM_TEXTURE_FILTER_LINEAR


static const struct {
    SceGxmTextureFilter filter;
    SceGxmTextureAddrMode addr;
    float from, to;
} cases[] = {
    { POINT, A(CLAMP), 0, 1 },          { LINEAR, A(CLAMP), 0, 1 },       { POINT, A(REPEAT), -1, 2 },
    { POINT, A(MIRROR), -1, 2 },        { POINT, A(CLAMP), -1, 2 },       { POINT, A(MIRROR_CLAMP), -1, 2 },
    { LINEAR, A(REPEAT), -1, 2 },       { LINEAR, A(MIRROR), -1, 2 },     { LINEAR, A(CLAMP), -1, 2 },
    { LINEAR, A(MIRROR_CLAMP), -1, 2 }, { LINEAR, A(REPEAT), 0.3f, 0.7f }, { POINT, A(REPEAT), 0.3f, 0.7f },
};

#define CELL 160
#define COLUMNS (RENDER_WIDTH / CELL)
#define SMALL 8
#define CHECKER 64

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *textured = render_texture_program(NULL);

    unsigned int *small = render_texture_memory(SMALL * SMALL * 4);
    unsigned int *checker = render_texture_memory(CHECKER * CHECKER * 4);
    if (small == NULL || checker == NULL) {
        PRINT("allocate the textures", -1);
        test_exit();
    }
    // Red across, green down, blue where they differ, and alpha 0xFF, 0x80 and 0 by turns
    static const unsigned int alphas[3] = { 0xFF000000, 0x80000000, 0x00000000 };
    for (int y = 0; y < SMALL; y++)
        for (int x = 0; x < SMALL; x++)
            small[y * SMALL + x] = alphas[(x + y) % 3] | ((x ^ y) * 36) << 16 | (y * 36) << 8 | x * 36;
    for (int y = 0; y < CHECKER; y++)
        for (int x = 0; x < CHECKER; x++)
            checker[y * CHECKER + x] = ((x ^ y) & 1) ? 0xFFFFFFFF : (x & 8) ? 0xFF0000FF : 0xFF000000;

    // Kept until the frame ends, like the textures of a game
    const int count = sizeof(cases) / sizeof(cases[0]);
    static SceGxmTexture textures[sizeof(cases) / sizeof(cases[0])], checkers[2];
    for (int i = 0; i < count; i++) {
        PRINTF(sceGxmTextureInitLinear(&textures[i], small, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SMALL, SMALL, 0),
               "set up texture %d", i);
        sceGxmTextureSetMinFilter(&textures[i], cases[i].filter);
        sceGxmTextureSetMagFilter(&textures[i], cases[i].filter);
        sceGxmTextureSetUAddrMode(&textures[i], cases[i].addr);
        sceGxmTextureSetVAddrMode(&textures[i], cases[i].addr);
    }
    for (int f = 0; f < 2; f++) {
        PRINTF(sceGxmTextureInitLinear(&checkers[f], checker, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, CHECKER, CHECKER,
                                       0),
               "set up checkerboard %d", f);
        sceGxmTextureSetMinFilter(&checkers[f], f ? LINEAR : POINT);
        sceGxmTextureSetMagFilter(&checkers[f], f ? LINEAR : POINT);
    }

    PRINT("begin a frame", render_begin());
    render_clear(0xFF404040);
    for (int i = 0; i < count; i++) {
        const float x = (i % COLUMNS) * CELL + 8, y = (i / COLUMNS) * CELL + 8;
        render_textured_rect(textured, &textures[i], x, y, x + CELL - 16, y + CELL - 16, cases[i].from,
                             cases[i].from, cases[i].to, cases[i].to);
    }
    // The checkerboard at a half, a quarter, a third and three quarters of its size
    static const float sizes[4] = { CHECKER / 2, CHECKER / 4, CHECKER / 3.0f, CHECKER * 0.75f };
    for (int f = 0; f < 2; f++) {
        float x = 8;
        const float y = 2 * CELL + 8 + f * 104;
        for (int s = 0; s < 4; s++) {
            render_textured_rect(textured, &checkers[f], x, y, x + sizes[s], y + sizes[s], 0, 0, 1, 1);
            x += sizes[s] + 24;
        }
    }
    PRINT("draw each sampling case", render_end());
    test_capture_display("cases");

    test_exit();
    return 0;
}
