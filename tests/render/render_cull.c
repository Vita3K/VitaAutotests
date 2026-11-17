// Culling: a row for each cull mode, with a triangle wound each way on the screen, a strip whose
// triangles alternate their winding, and fans wound each way.
#include "capture.h"
#include "render.h"
#include "test.h"

static SceGxmFragmentProgram *opaque;

static void shapes(float y) {
    // Clockwise and counterclockwise on the screen, y going down
    const render_color_vertex cw[3] = {
        { 20, y, 0.5f, 0xFF0000FF }, { 160, y, 0.5f, 0xFF0000FF }, { 20, y + 140, 0.5f, 0xFF0000FF } };
    const render_color_vertex ccw[3] = {
        { 180, y, 0.5f, 0xFF00FF00 }, { 180, y + 140, 0.5f, 0xFF00FF00 }, { 320, y, 0.5f, 0xFF00FF00 } };
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLES, cw, 3);
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLES, ccw, 3);

    // A strip of six triangles, each in its own color so a culled one shows
    render_color_vertex strip[8];
    for (int i = 0; i < 8; i++) {
        static const unsigned int colors[4] = { 0xFFFF0000, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF };
        strip[i] = (render_color_vertex){ 340 + (i / 2) * 60, y + (i & 1) * 140, 0.5f, colors[(i / 2) % 4] };
    }
    render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, strip, 8);

    // Fans going around each way
    for (int f = 0; f < 2; f++) {
        const float cx = 640 + f * 160 + 70, cy = y + 70;
        render_color_vertex fan[6];
        fan[0] = (render_color_vertex){ cx, cy, 0.5f, 0xFFFFFFFF };
        static const float dx[5] = { 0, 70, 0, -70, 0 }, dy[5] = { -70, 0, 70, 0, -70 };
        for (int i = 0; i < 5; i++) {
            const float sign = f ? -1.0f : 1.0f;
            fan[1 + i] = (render_color_vertex){ cx + sign * dx[i], cy + dy[i], 0.5f, i & 1 ? 0xFF808080 : 0xFF0080FF };
        }
        render_colored(opaque, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, fan, 6);
    }
}

int main(void) {
    const int setup = render_init();
    PRINT("set up rendering", setup);
    if (setup < 0)
        test_exit();
    opaque = render_color_program(NULL);

    static const SceGxmCullMode modes[3] = { SCE_GXM_CULL_NONE, SCE_GXM_CULL_CW, SCE_GXM_CULL_CCW };
    PRINT("begin a frame", render_begin());
    render_clear(0xFF000000);
    for (int m = 0; m < 3; m++) {
        sceGxmSetCullMode(render_context, modes[m]);
        shapes(20 + m * 175);
    }
    PRINT("draw each cull mode", render_end());
    test_capture_display("modes");

    test_exit();
    return 0;
}
