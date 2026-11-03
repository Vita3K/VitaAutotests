// Which pixels a primitive covers: rectangles at fractions of a pixel, triangles sharing an edge,
// slivers thinner than a pixel, and points and lines of a few widths. Every primitive is in one
// flat color, so only coverage can differ.
#include "capture.h"
#include "render.h"
#include "test.h"

#define WHITE 0xFFFFFFFF
#define RED 0xFF0000FF
#define GREEN 0xFF00FF00
#define BLUE 0xFFFF0000

static SceGxmFragmentProgram *opaque;

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int color) {
    const render_color_vertex vertices[3] = {
        { x0, y0, 0.5f, color },
        { x1, y1, 0.5f, color },
        { x2, y2, 0.5f, color },
    };
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLES, vertices, 3);
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    opaque = render_color_program(NULL);

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

    // Two triangles sharing their diagonal, and four sharing a center, so a pixel drawn twice or not
    // at all shows on the edge
    triangle(80, 16, 200, 16, 80, 136, RED);
    triangle(200, 16, 200, 136, 80, 136, GREEN);
    triangle(220, 16, 340, 16, 280, 76, RED);
    triangle(340, 16, 340, 136, 280, 76, GREEN);
    triangle(340, 136, 220, 136, 280, 76, BLUE);
    triangle(220, 136, 220, 16, 280, 76, WHITE);
    // The same, with every vertex at a fraction of a pixel
    triangle(360.3f, 16.7f, 480.6f, 16.2f, 360.9f, 136.4f, RED);
    triangle(480.6f, 16.2f, 480.1f, 136.8f, 360.9f, 136.4f, GREEN);

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

    // Points and lines, 1, 2, 3 and 8 pixels wide
    static const unsigned int widths[] = { 1, 2, 3, 8 };
    for (int w = 0; w < 4; w++) {
        sceGxmSetFrontPointLineWidth(render_context, widths[w]);
        sceGxmSetBackPointLineWidth(render_context, widths[w]);
        const float y = 220 + w * 80;
        render_color_vertex points[8];
        for (int i = 0; i < 8; i++)
            points[i] = (render_color_vertex){ 24 + i * 24 + i * 0.125f, y + i * 0.125f, 0.5f, WHITE };
        render_colored(opaque, SCE_GXM_PRIMITIVE_POINTS, points, 8);
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
    PRINT("draw the primitives", render_end());
    test_capture_display("coverage");

    test_exit();
    return 0;
}
