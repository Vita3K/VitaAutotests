// Which pixels a primitive covers: rectangles at fractions of a pixel, triangles sharing an edge,
// slivers thinner than a pixel, and lines of a few widths. Every primitive is in one
// flat color, so only coverage can differ. The triangles that share an edge add their colors, so a
// pixel that two of them cover shows the sum instead of the color drawn last.
#include "capture.h"
#include "render.h"
#include "test.h"

#define WHITE 0xFFFFFFFF
#define RED 0xFF0000FF
#define GREEN 0xFF00FF00
#define BLUE 0xFFFF0000
// Half of each channel, and a quarter of all three, which add up without saturating
#define HALF_RED 0xFF000080
#define HALF_GREEN 0xFF008000
#define HALF_BLUE 0xFF800000
#define QUARTER_GRAY 0xFF404040

static SceGxmFragmentProgram *opaque, *additive;

static void triangle_with(SceGxmFragmentProgram *program, float x0, float y0, float x1, float y1, float x2, float y2,
                          unsigned int color) {
    const render_color_vertex vertices[3] = {
        { x0, y0, 0.5f, color },
        { x1, y1, 0.5f, color },
        { x2, y2, 0.5f, color },
    };
    render_colored(program, SCE_GXM_PRIMITIVE_TRIANGLES, vertices, 3);
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int color) {
    triangle_with(opaque, x0, y0, x1, y1, x2, y2, color);
}

// A triangle whose color adds to the frame's, keeping its alpha
static void shared_edge_triangle(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int color) {
    triangle_with(additive, x0, y0, x1, y1, x2, y2, color);
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    opaque = render_color_program(NULL);
    const SceGxmBlendInfo add = { SCE_GXM_COLOR_MASK_ALL,  SCE_GXM_BLEND_FUNC_ADD,  SCE_GXM_BLEND_FUNC_ADD,
                                  SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ZERO,
                                  SCE_GXM_BLEND_FACTOR_ONE };
    additive = render_color_program(&add);

    PRINT("begin a frame", render_begin());
    render_clear(0xFF000000);

    // Rectangles starting at each quarter of a pixel, and 1, 0.5 and 0.25 pixels wide and high
    static const float sizes[] = { 1.0f, 0.5f, 0.25f, 2.75f };
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            const float x = 16 + j * 12 + i * 0.25f, y = 16 + i * 12 + i * 0.25f;
            render_rect(opaque, x, y, x + sizes[j], y + sizes[j], 0.5f, WHITE);
        }
    }

    // Two triangles sharing their diagonal, and four sharing a center, so a pixel drawn twice shows as
    // the sum of two colors on the edge, and one drawn by none as black
    shared_edge_triangle(80, 16, 200, 16, 80, 136, HALF_RED);
    shared_edge_triangle(200, 16, 200, 136, 80, 136, HALF_GREEN);
    shared_edge_triangle(220, 16, 340, 16, 280, 76, HALF_RED);
    shared_edge_triangle(340, 16, 340, 136, 280, 76, HALF_GREEN);
    shared_edge_triangle(340, 136, 220, 136, 280, 76, HALF_BLUE);
    shared_edge_triangle(220, 136, 220, 16, 280, 76, QUARTER_GRAY);
    // The same, with every vertex at a fraction of a pixel
    shared_edge_triangle(360.3f, 16.7f, 480.6f, 16.2f, 360.9f, 136.4f, HALF_RED);
    shared_edge_triangle(480.6f, 16.2f, 480.1f, 136.8f, 360.9f, 136.4f, HALF_GREEN);

    // Slivers: long triangles thinner than a pixel, at a few slopes
    for (int i = 0; i < 8; i++) {
        const float x = 500 + i * 56;
        triangle(x, 16, x + 0.6f, 16, x + 4 + i * 6, 136, WHITE);
        triangle(x + 20, 16, x + 20 + i * 4, 136, x + 20.4f + i * 4, 136, RED);
    }

    // Triangles a vertex at a time across a pixel center
    for (int i = 0; i < 16; i++) {
        const float x = 16 + i * 30, f = i / 16.0f;
        triangle(x + f, 160 + f, x + 20 + f, 160, x, 180 + f, i & 1 ? GREEN : BLUE);
    }

    // Lines 1, 2, 3 and 8 pixels wide, which are only drawn in the line polygon mode: the triangle fill
    // mode draws nothing for them. No points, since the size of a point comes from the vertex program,
    // and the one these draw with doesn't write one, so a Vita draws each at a size of its own.
    sceGxmSetFrontPolygonMode(render_context, SCE_GXM_POLYGON_MODE_LINE);
    sceGxmSetBackPolygonMode(render_context, SCE_GXM_POLYGON_MODE_LINE);
    static const unsigned int widths[] = { 1, 2, 3, 8 };
    for (int w = 0; w < 4; w++) {
        sceGxmSetFrontPointLineWidth(render_context, widths[w]);
        sceGxmSetBackPointLineWidth(render_context, widths[w]);
        const float y = 220 + w * 80;
        // Lines in each direction from a center, and nearly flat ones
        render_color_vertex lines[16];
        for (int i = 0; i < 4; i++) {
            static const float dx[4] = { 30, 30, 0, -30 }, dy[4] = { 0, 30, 30, 30 };
            lines[i * 2] = (render_color_vertex){ 300 + i * 80, y, 0.5f, RED };
            lines[i * 2 + 1] = (render_color_vertex){ 300 + i * 80 + dx[i], y + dy[i], 0.5f, RED };
        }
        for (int i = 0; i < 4; i++) {
            lines[8 + i * 2] = (render_color_vertex){ 640 + i * 0.25f, y + i * 16, 0.5f, GREEN };
            lines[8 + i * 2 + 1] = (render_color_vertex){ 900 + i * 0.25f, y + i * 16 + 3, 0.5f, GREEN };
        }
        render_colored(opaque, SCE_GXM_PRIMITIVE_LINES, lines, 16);
    }
    sceGxmSetFrontPolygonMode(render_context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetBackPolygonMode(render_context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    PRINT("draw the primitives", render_end());
    test_capture_display("coverage");

    test_exit();
    return 0;
}
