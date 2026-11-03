// Drawing with libgxm for the render tests, with the shaders of vita2d: a full screen clear to a
// uniform color, vertex colors, and a texture. A test draws a frame between render_begin() and
// render_end(), which shows it on the display, then captures it with test_capture_display().
//
// Positions are in pixels from the top left of the 960x544 frame, with a depth from 0 to 1. A frame
// starts with depth 1 and stencil 0 everywhere, and render_begin() sets the same state each time: no
// culling, no two sided mode, the depth test always passes and doesn't write, the stencil test always
// passes and keeps the stencil.
#pragma once

#include <psp2/gxm.h>

#define RENDER_WIDTH 960
#define RENDER_HEIGHT 544
// The most vertices a single draw takes
#define RENDER_MAX_VERTICES 4096

// The shader binaries, from shaders.c, which the build makes from common/shaders/
extern const SceGxmProgram *const clear_v_gxp;
extern const SceGxmProgram *const clear_f_gxp;
extern const SceGxmProgram *const color_v_gxp;
extern const SceGxmProgram *const color_f_gxp;
extern const SceGxmProgram *const texture_v_gxp;
extern const SceGxmProgram *const texture_f_gxp;

// A color is 0xAABBGGRR, as A8B8G8R8 holds it
typedef struct render_color_vertex {
    float x, y, z;
    unsigned int color;
} render_color_vertex;

typedef struct render_texture_vertex {
    float x, y, z;
    float u, v;
} render_texture_vertex;

extern SceGxmContext *render_context;

// Sets up libgxm, the frame buffers and the shaders. Returns 0, or the first error, after it printed
// which step failed.
int render_init(void);

// Fragment programs for the color and texture shaders, which blend as blend says, or not at all
// for NULL. A program that can't be made is NULL, and its error is what render_end() returns.
SceGxmFragmentProgram *render_color_program(const SceGxmBlendInfo *blend);
SceGxmFragmentProgram *render_texture_program(const SceGxmBlendInfo *blend);

// Memory the GPU reads, for textures. Returns NULL when it's used up.
void *render_texture_memory(unsigned int size);

// Starts a frame, and sets the state above
int render_begin(void);

// Fills the frame with color, drawn with the state as it is
void render_clear(unsigned int color);

// Draws vertices with vertex colors
void render_colored(SceGxmFragmentProgram *program, SceGxmPrimitiveType type, const render_color_vertex *vertices,
                    int count);

// Draws a rectangle from (x0, y0) to (x1, y1), in one color
void render_rect(SceGxmFragmentProgram *program, float x0, float y0, float x1, float y1, float z, unsigned int color);

// Draws vertices with texture coordinates, sampling texture
void render_textured(SceGxmFragmentProgram *program, const SceGxmTexture *texture, SceGxmPrimitiveType type,
                     const render_texture_vertex *vertices, int count);

// Draws texture over the rectangle from (x0, y0) to (x1, y1), from (u0, v0) to (u1, v1) of it
void render_textured_rect(SceGxmFragmentProgram *program, const SceGxmTexture *texture, float x0, float y0, float x1,
                          float y1, float u0, float v0, float u1, float v1);

// Ends the frame, waits for the GPU and shows the frame on the display. Returns 0, or the first error
// of a call since the last render_end().
int render_end(void);
