// The stencil test and its operations. Each cell starts with stencil 0xFF, 0x80 and 0x00 in its
// left, middle and right third, then has an operation or test applied below its top quarter. The
// stencil is then drawn into the red channel, a bit at a time with additive blending, so the
// capture holds the exact stencil value of every pixel.
#include "capture.h"
#include "render.h"
#include "test.h"

#define OP(name) SCE_GXM_STENCIL_OP_##name
#define FUNC(name) SCE_GXM_STENCIL_FUNC_##name

static const struct {
    SceGxmStencilFunc func;
    SceGxmStencilOp stencil_fail, depth_fail, depth_pass;
    unsigned char ref, compare_mask, write_mask;
    // Whether the depth test fails, to reach depth_fail
    int depth_fails;
} cases[] = {
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(KEEP), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(ZERO), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(REPLACE), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(INCR), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(DECR), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(INVERT), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(INCR_WRAP), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(DECR_WRAP), 0x5A, 0xFF, 0xFF, 0 },
    // Write masks
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(REPLACE), 0x5A, 0xFF, 0x0F, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(INVERT), 0x5A, 0xFF, 0xF0, 0 },
    // The stencil and depth fail operations
    { FUNC(NEVER), OP(INCR_WRAP), OP(KEEP), OP(ZERO), 0x5A, 0xFF, 0xFF, 0 },
    { FUNC(ALWAYS), OP(KEEP), OP(INVERT), OP(ZERO), 0x5A, 0xFF, 0xFF, 1 },
    // Comparisons with 0x80, passes zero the stencil and fails invert it
    { FUNC(LESS), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    { FUNC(GREATER), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    { FUNC(EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    { FUNC(LESS_EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    { FUNC(GREATER_EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    { FUNC(NOT_EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0xFF, 0xFF, 0 },
    // A compare mask, which only looks at the top bit
    { FUNC(EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x80, 0x80, 0xFF, 0 },
    { FUNC(EQUAL), OP(INVERT), OP(KEEP), OP(ZERO), 0x8F, 0xF0, 0xFF, 0 },
};

static const unsigned char thirds[3] = { 0xFF, 0x80, 0x00 };

#define COLUMNS 5
#define CELL_W (RENDER_WIDTH / COLUMNS)
#define CELL_H (RENDER_HEIGHT / 4)

static void stencil(SceGxmStencilFunc func, SceGxmStencilOp stencil_fail, SceGxmStencilOp depth_fail,
                    SceGxmStencilOp depth_pass, unsigned char ref, unsigned char compare_mask,
                    unsigned char write_mask) {
    sceGxmSetFrontStencilFunc(render_context, func, stencil_fail, depth_fail, depth_pass, compare_mask, write_mask);
    sceGxmSetBackStencilFunc(render_context, func, stencil_fail, depth_fail, depth_pass, compare_mask, write_mask);
    sceGxmSetFrontStencilRef(render_context, ref);
    sceGxmSetBackStencilRef(render_context, ref);
}

static void depth_func(SceGxmDepthFunc func) {
    sceGxmSetFrontDepthFunc(render_context, func);
    sceGxmSetBackDepthFunc(render_context, func);
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *opaque = render_color_program(NULL);
    const SceGxmBlendInfo no_color = { SCE_GXM_COLOR_MASK_NONE,    SCE_GXM_BLEND_FUNC_NONE,   SCE_GXM_BLEND_FUNC_NONE,
                                       SCE_GXM_BLEND_FACTOR_ONE,   SCE_GXM_BLEND_FACTOR_ZERO, SCE_GXM_BLEND_FACTOR_ONE,
                                       SCE_GXM_BLEND_FACTOR_ZERO };
    SceGxmFragmentProgram *stencil_only = render_color_program(&no_color);
    const SceGxmBlendInfo add = { SCE_GXM_COLOR_MASK_ALL,  SCE_GXM_BLEND_FUNC_ADD,  SCE_GXM_BLEND_FUNC_ADD,
                                  SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ONE,
                                  SCE_GXM_BLEND_FACTOR_ONE };
    SceGxmFragmentProgram *additive = render_color_program(&add);

    PRINT("begin a frame", render_begin());
    render_clear(0xFF000000);
    const int count = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < count; i++) {
        const float x = (i % COLUMNS) * CELL_W + 4, y = (i / COLUMNS) * CELL_H + 4;
        const float w = CELL_W - 8, h = CELL_H - 8;
        for (int t = 0; t < 3; t++) {
            stencil(FUNC(ALWAYS), OP(KEEP), OP(KEEP), OP(REPLACE), thirds[t], 0xFF, 0xFF);
            render_rect(stencil_only, x + t * w / 3, y, x + (t + 1) * w / 3, y + h, 0.5f, 0);
        }
        stencil(cases[i].func, cases[i].stencil_fail, cases[i].depth_fail, cases[i].depth_pass, cases[i].ref,
                cases[i].compare_mask, cases[i].write_mask);
        depth_func(cases[i].depth_fails ? SCE_GXM_DEPTH_FUNC_NEVER : SCE_GXM_DEPTH_FUNC_ALWAYS);
        render_rect(stencil_only, x, y + h / 4, x + w, y + h, 0.5f, 0);
        depth_func(SCE_GXM_DEPTH_FUNC_ALWAYS);
        // Draws the stencil into the red channel, over the black the frame was cleared to
        for (int bit = 0; bit < 8; bit++) {
            stencil(FUNC(EQUAL), OP(KEEP), OP(KEEP), OP(KEEP), 1 << bit, 1 << bit, 0);
            render_rect(additive, x, y, x + w, y + h, 0.5f, 1u << bit);
        }
    }
    PRINT("draw each stencil case", render_end());
    test_capture_display("cases");

    test_exit();
    return 0;
}
