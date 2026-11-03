// Clearing to a color from a fragment uniform, and rectangles in flat colors, which need no rounding
// or interpolation: each pixel holds the exact color drawn, alpha included.
#include "capture.h"
#include "render.h"
#include "test.h"

static const unsigned int colors[] = {
    // Primaries, secondaries, black and white
    0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF00FFFF, 0xFFFF00FF, 0xFFFFFF00, 0xFF000000, 0xFFFFFFFF,
    // Grays
    0xFF010101, 0xFF202020, 0xFF7F7F7F, 0xFF808080, 0xFFC0C0C0, 0xFFFEFEFE, 0xFF123456, 0xFF654321,
    // Alpha, written as it is without blending
    0x00FFFFFF, 0x01FFFFFF, 0x7FFFFFFF, 0x80FFFFFF, 0xFE808080, 0x00000000, 0x80000000, 0x40C08020,
    // Each channel alone at levels that round differently from 5 or 6 bits
    0xFF000007, 0xFF000700, 0xFF070000, 0xFF000084, 0xFF008400, 0xFF840000, 0xFF0000F8, 0xFFF8FC00,
};

#define COLUMNS 8
#define CELL_W (RENDER_WIDTH / COLUMNS)
#define CELL_H (RENDER_HEIGHT / 4)

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *opaque = render_color_program(NULL);

    PRINT("begin a frame", render_begin());
    render_clear(0xFF336699);
    PRINT("clear it", render_end());
    test_capture_display("clear");

    PRINT("begin a frame", render_begin());
    render_clear(0x80402010);
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        const float x = (i % COLUMNS) * CELL_W, y = (i / COLUMNS) * CELL_H;
        render_rect(opaque, x + 8, y + 8, x + CELL_W - 8, y + CELL_H - 8, 0.5f, colors[i]);
    }
    PRINT("draw rectangles in flat colors", render_end());
    test_capture_display("colors");

    test_exit();
    return 0;
}
