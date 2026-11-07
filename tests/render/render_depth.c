// The depth test: for each depth function, three bands at depths 0.25, 0.5 and 0.75 drawn over a
// rectangle at depth 0.5 on the left and the background depth of 1 on the right, so each band
// shows where the function passed. Then what doesn't write depth, and depths outside 0 to 1.
#include "capture.h"
#include "render.h"
#include "test.h"

#define F(name) SCE_GXM_DEPTH_FUNC_##name

static const SceGxmDepthFunc funcs[] = { F(NEVER), F(LESS),      F(EQUAL),  F(LESS_EQUAL),
                                         F(GREATER), F(NOT_EQUAL), F(GREATER_EQUAL), F(ALWAYS) };
static const float band_z[3] = { 0.25f, 0.5f, 0.75f };
static const unsigned int band_color[3] = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000 };

#define COLUMNS 5
#define CELL_W (RENDER_WIDTH / COLUMNS)
#define CELL_H (RENDER_HEIGHT / 2)

static SceGxmFragmentProgram *opaque;

static void depth_state(SceGxmDepthFunc func, SceGxmDepthWriteMode write) {
    sceGxmSetFrontDepthFunc(render_context, func);
    sceGxmSetBackDepthFunc(render_context, func);
    sceGxmSetFrontDepthWriteEnable(render_context, write);
    sceGxmSetBackDepthWriteEnable(render_context, write);
}

// Draws the rectangle at 0.5 on the left half of cell i, writing depth if write, then the bands
// with func
static void cell(int i, SceGxmDepthWriteMode write, SceGxmDepthFunc func) {
    const float x = (i % COLUMNS) * CELL_W + 8, y = (i / COLUMNS) * CELL_H + 8;
    const float w = CELL_W - 16, h = CELL_H - 16;
    depth_state(SCE_GXM_DEPTH_FUNC_ALWAYS, write);
    render_rect(opaque, x, y, x + w / 2, y + h, 0.5f, 0xFF808080);
    depth_state(func, SCE_GXM_DEPTH_WRITE_DISABLED);
    for (int b = 0; b < 3; b++)
        render_rect(opaque, x + 8, y + 8 + b * (h - 16) / 3, x + w - 8, y + 8 + (b + 1) * (h - 16) / 3 - 4, band_z[b],
                    band_color[b]);
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    opaque = render_color_program(NULL);

    PRINT("begin a frame", render_begin());
    render_clear(0xFF000000);
    for (int i = 0; i < 8; i++)
        cell(i, SCE_GXM_DEPTH_WRITE_ENABLED, funcs[i]);
    // The rectangle doesn't write its depth, so a band that passes less than 1 covers it
    cell(8, SCE_GXM_DEPTH_WRITE_DISABLED, F(LESS));

    // Depths below 0, at 0, at 1 and past 1, drawn with the test always passing, then the same depth
    // tested for equal over that and over the background, which shows what each one wrote
    const float x = (9 % COLUMNS) * CELL_W + 8, y = (9 / COLUMNS) * CELL_H + 8;
    const float w = CELL_W - 16, h = CELL_H - 16;
    static const float outside_z[4] = { -0.5f, 0.0f, 1.0f, 1.5f };
    for (int b = 0; b < 4; b++) {
        const float top = y + b * h / 4, bottom = y + (b + 1) * h / 4 - 4;
        depth_state(F(ALWAYS), SCE_GXM_DEPTH_WRITE_ENABLED);
        render_rect(opaque, x, top, x + w / 2, bottom, outside_z[b], 0xFF00FFFF);
        depth_state(F(EQUAL), SCE_GXM_DEPTH_WRITE_DISABLED);
        render_rect(opaque, x + w / 4, top, x + w, bottom, outside_z[b], 0xFFFF00FF);
    }
    PRINT("draw each depth function", render_end());
    test_capture_display("funcs");

    test_exit();
    return 0;
}
