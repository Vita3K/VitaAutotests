// Blending: for each blend function and factors, a color is drawn over four stripes of destination
// colors, with alpha 0x80 in the top half and alpha from 0 to 255 in the bottom half, so the
// capture shows each mode over every source alpha. Then the color mask.
#include "capture.h"
#include "render.h"
#include "test.h"

#define F(name) SCE_GXM_BLEND_FACTOR_##name
#define FN(name) SCE_GXM_BLEND_FUNC_##name
#define MASK_ALL SCE_GXM_COLOR_MASK_ALL

static const struct {
    SceGxmBlendInfo info;
    const char *what;
} modes[] = {
    { { MASK_ALL, FN(NONE), FN(NONE), F(ONE), F(ZERO), F(ONE), F(ZERO) }, "no blending" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(SRC_ALPHA), F(ONE_MINUS_SRC_ALPHA), F(ONE), F(ONE_MINUS_SRC_ALPHA) },
      "alpha blending" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(ONE), F(ONE), F(ONE), F(ONE) }, "add" },
    { { MASK_ALL, FN(SUBTRACT), FN(SUBTRACT), F(ONE), F(ONE), F(ONE), F(ONE) }, "subtract" },
    { { MASK_ALL, FN(REVERSE_SUBTRACT), FN(REVERSE_SUBTRACT), F(ONE), F(ONE), F(ONE), F(ONE) }, "reverse subtract" },
    { { MASK_ALL, FN(MIN), FN(MIN), F(ONE), F(ONE), F(ONE), F(ONE) }, "min" },
    { { MASK_ALL, FN(MAX), FN(MAX), F(ONE), F(ONE), F(ONE), F(ONE) }, "max" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(DST_COLOR), F(ZERO), F(DST_ALPHA), F(ZERO) }, "multiply" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(ONE_MINUS_DST_COLOR), F(ONE), F(ONE_MINUS_DST_ALPHA), F(ONE) }, "screen" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(SRC_COLOR), F(ONE_MINUS_SRC_COLOR), F(SRC_ALPHA), F(ONE_MINUS_SRC_ALPHA) },
      "source color factors" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(DST_ALPHA), F(ONE_MINUS_DST_ALPHA), F(ONE), F(ZERO) }, "destination alpha" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(ONE_MINUS_DST_ALPHA), F(DST_ALPHA), F(ZERO), F(ONE) },
      "one minus destination alpha" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(SRC_ALPHA_SATURATE), F(ONE), F(ONE), F(ONE) }, "source alpha saturate" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(DST_ALPHA_SATURATE), F(ONE), F(ONE), F(ONE) }, "destination alpha saturate" },
    { { MASK_ALL, FN(ADD), FN(ADD), F(ZERO), F(SRC_ALPHA), F(ZERO), F(ONE) }, "destination times source alpha" },
    { { MASK_ALL, FN(ADD), FN(SUBTRACT), F(SRC_ALPHA), F(ONE_MINUS_SRC_ALPHA), F(ONE), F(ONE) },
      "alpha blending, alpha subtracted" },
    { { SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_A, FN(NONE), FN(NONE), F(ONE), F(ZERO), F(ONE), F(ZERO) },
      "red and alpha masked in" },
    { { SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B, FN(ADD), FN(ADD), F(SRC_ALPHA), F(ONE_MINUS_SRC_ALPHA),
        F(ONE), F(ZERO) },
      "green and blue masked in, blended" },
    { { SCE_GXM_COLOR_MASK_NONE, FN(NONE), FN(NONE), F(ONE), F(ZERO), F(ONE), F(ZERO) }, "everything masked out" },
    { { SCE_GXM_COLOR_MASK_A, FN(ADD), FN(ADD), F(ONE), F(ONE), F(ONE), F(ONE) }, "alpha masked in, added" },
};

#define COLUMNS 5
#define CELL_W (RENDER_WIDTH / COLUMNS)
#define CELL_H (RENDER_HEIGHT / 4)
#define SOURCE 0x00FF8040

static const unsigned int destinations[4] = { 0xFF0000FF, 0x8000FF00, 0xFFFFFFFF, 0x00000000 };

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *opaque = render_color_program(NULL);
    const int count = sizeof(modes) / sizeof(modes[0]);
    SceGxmFragmentProgram *programs[sizeof(modes) / sizeof(modes[0])];
    for (int i = 0; i < count; i++) {
        programs[i] = render_color_program(&modes[i].info);
        PRINTF(programs[i] != NULL, "make a fragment program for %s", modes[i].what);
    }

    PRINT("begin a frame", render_begin());
    render_clear(0xFF404040);
    for (int i = 0; i < count; i++) {
        const float x = (i % COLUMNS) * CELL_W + 4, y = (i / COLUMNS) * CELL_H + 4;
        const float w = CELL_W - 8, h = CELL_H - 8;
        for (int d = 0; d < 4; d++)
            render_rect(opaque, x + d * w / 4, y, x + (d + 1) * w / 4, y + h, 0.5f, destinations[d]);
        render_rect(programs[i], x + 4, y + 4, x + w - 4, y + h / 2, 0.5f, SOURCE | 0x80000000);
        const render_color_vertex ramp[4] = {
            { x + 4, y + h / 2, 0.5f, SOURCE },
            { x + w - 4, y + h / 2, 0.5f, SOURCE },
            { x + 4, y + h - 4, 0.5f, SOURCE | 0xFF000000 },
            { x + w - 4, y + h - 4, 0.5f, SOURCE | 0xFF000000 },
        };
        render_colored(programs[i], SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, ramp, 4);
    }
    PRINT("draw each blend mode", render_end());
    test_capture_display("modes");

    test_exit();
    return 0;
}
