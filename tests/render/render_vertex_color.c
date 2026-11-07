// Vertex colors interpolated across triangles, strips and fans, alpha included, which shows how
// the GPU interpolates and rounds them to 8 bits.
#include "capture.h"
#include "render.h"
#include "test.h"

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    SceGxmFragmentProgram *opaque = render_color_program(NULL);

    PRINT("begin a frame", render_begin());
    render_clear(0xFF000000);

    // A triangle with red, green and blue corners
    const render_color_vertex rgb[3] = {
        { 20, 20, 0.5f, 0xFF0000FF },
        { 300, 20, 0.5f, 0xFF00FF00 },
        { 160, 260, 0.5f, 0xFFFF0000 },
    };
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLES, rgb, 3);

    // A quad as a strip with four corner colors, whose split shows in the gradient
    const render_color_vertex quad[4] = {
        { 320, 20, 0.5f, 0xFF0000FF },
        { 620, 20, 0.5f, 0xFF00FF00 },
        { 320, 260, 0.5f, 0xFFFF0000 },
        { 620, 260, 0.5f, 0xFFFFFFFF },
    };
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, quad, 4);

    // A fan around a white center
    render_color_vertex fan[10];
    fan[0] = (render_color_vertex){ 790, 140, 0.5f, 0xFFFFFFFF };
    static const float rim_x[9] = { 790, 900, 930, 900, 790, 680, 650, 680, 790 };
    static const float rim_y[9] = { 20, 50, 140, 230, 260, 230, 140, 50, 20 };
    static const unsigned int rim_color[9] = { 0xFF0000FF, 0xFF0080FF, 0xFF00FFFF, 0xFF00FF00, 0xFFFFFF00,
                                               0xFFFF0000, 0xFFFF00FF, 0xFF8000FF, 0xFF0000FF };
    for (int i = 0; i < 9; i++)
        fan[1 + i] = (render_color_vertex){ rim_x[i], rim_y[i], 0.5f, rim_color[i] };
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, fan, 10);

    // Long, shallow gradients, where each step of 1 in a channel spans a few pixels: black to white,
    // a channel at a time, and alpha from 0 to 255
    static const unsigned int ends[5][2] = {
        { 0xFF000000, 0xFFFFFFFF }, { 0xFF000000, 0xFF0000FF }, { 0xFF000000, 0xFF00FF00 },
        { 0xFF000000, 0xFFFF0000 }, { 0x00FFFFFF, 0xFFFFFFFF },
    };
    for (int i = 0; i < 5; i++) {
        const float y = 280 + i * 40;
        const render_color_vertex bar[4] = {
            { 20, y, 0.5f, ends[i][0] },
            { 940, y, 0.5f, ends[i][1] },
            { 20, y + 32, 0.5f, ends[i][0] },
            { 940, y + 32, 0.5f, ends[i][1] },
        };
        render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, bar, 4);
    }
    PRINT("draw the gradients", render_end());
    test_capture_display("gradients");

    test_exit();
    return 0;
}
