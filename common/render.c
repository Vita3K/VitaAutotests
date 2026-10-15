// Sets up libgxm the way vita2d and most games do: two frame buffers in CDRAM that the display
// queue flips between, a tiled depth and stencil surface, and the shader patcher.
#include <psp2/display.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>

#include "render.h"
#include "test.h"

#define BUFFER_COUNT 2
#define STRIDE RENDER_WIDTH
#define DEPTH_STRIDE ((RENDER_WIDTH + SCE_GXM_TILE_SIZEX - 1) & ~(SCE_GXM_TILE_SIZEX - 1))
#define DEPTH_HEIGHT ((RENDER_HEIGHT + SCE_GXM_TILE_SIZEY - 1) & ~(SCE_GXM_TILE_SIZEY - 1))
#define PATCHER_MEM_SIZE (64 * 1024)
#define VERTEX_MEM_SIZE (1024 * 1024)
#define TEXTURE_MEM_SIZE (2 * 1024 * 1024)

SceGxmContext *render_context;

static SceGxmRenderTarget *target;
static void *color_data[BUFFER_COUNT];
static SceGxmColorSurface color_surfaces[BUFFER_COUNT];
static SceGxmSyncObject *syncs[BUFFER_COUNT];
static int back;
static SceGxmDepthStencilSurface depth_surface;
static SceGxmShaderPatcher *patcher;
static SceGxmShaderPatcherId clear_v_id, clear_f_id, color_v_id, color_f_id, texture_v_id, texture_f_id;
static SceGxmVertexProgram *clear_vertex, *color_vertex, *texture_vertex;
static SceGxmFragmentProgram *clear_fragment;
static const SceGxmProgramParameter *clear_color_param, *color_wvp_param, *texture_wvp_param;
static unsigned short *indices;
static float *clear_vertices;
static unsigned char *vertex_mem, *texture_mem;
static unsigned int vertex_used, texture_used;
static int render_error;

// Maps pixels to clip space, as a row vector times it, like vita2d's orthographic matrix: x from 0
// to the width goes to -1 to 1, y from 0 to the height goes to 1 to -1, and z stays as it is
static const float wvp[16] = {
    2.0f / RENDER_WIDTH, 0, 0, 0, 0, -2.0f / RENDER_HEIGHT, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1,
};

// What the display queue passes its callback
typedef struct display_data {
    void *address;
} display_data;

static void display_callback(const void *callback_data) {
    const display_data *data = callback_data;
    SceDisplayFrameBuf fb = { sizeof(fb), data->address, STRIDE, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, RENDER_WIDTH,
                              RENDER_HEIGHT };
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitSetFrameBuf();
}

static void check(int result) {
    if (result < 0 && render_error == 0)
        render_error = result;
}

// Allocates a memory block, rounded up to what its type needs, and returns its base, or NULL
static void *alloc_block(unsigned int type, unsigned int size, int *error) {
    const unsigned int align = type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? 256 * 1024 : 4 * 1024;
    size = (size + align - 1) & ~(align - 1);
    const SceUID block = sceKernelAllocMemBlock("render", type, size, NULL);
    void *base = NULL;
    *error = block < 0 ? block : sceKernelGetMemBlockBase(block, &base);
    return *error < 0 ? NULL : base;
}

// Allocates a memory block the GPU uses, mapped with attribs
static void *alloc_gpu(unsigned int type, unsigned int size, SceGxmMemoryAttribFlags attribs, int *error) {
    void *base = alloc_block(type, size, error);
    if (base != NULL)
        *error = sceGxmMapMemory(base, (size + 4095) & ~4095u, attribs);
    return *error < 0 ? NULL : base;
}

static void *patcher_host_alloc(void *user_data, SceSize size) {
    (void)user_data;
    return malloc(size);
}

static void patcher_host_free(void *user_data, void *mem) {
    (void)user_data;
    free(mem);
}

// Prints the step of render_init() that failed
#define STEP(what, call)                                \
    do {                                                \
        const int step_result_ = (call);                \
        if (step_result_ < 0) {                         \
            PRINT("render setup: " what, step_result_); \
            return step_result_;                        \
        }                                               \
    } while (0)

static int register_program(const SceGxmProgram *program, SceGxmShaderPatcherId *id) {
    const int checked = sceGxmProgramCheck(program);
    return checked < 0 ? checked : sceGxmShaderPatcherRegisterProgram(patcher, program, id);
}

static int find_param(const SceGxmProgram *program, const char *name, const SceGxmProgramParameter **param) {
    *param = sceGxmProgramFindParameterByName(program, name);
    return *param == NULL ? -1 : 0;
}

static int attribute(const SceGxmProgram *program, const char *name, unsigned short offset, unsigned char format,
                     unsigned char components, SceGxmVertexAttribute *attr) {
    const SceGxmProgramParameter *param;
    if (find_param(program, name, &param) < 0)
        return -1;
    attr->streamIndex = 0;
    attr->offset = offset;
    attr->format = format;
    attr->componentCount = components;
    attr->regIndex = sceGxmProgramParameterGetResourceIndex(param);
    return 0;
}

int render_init(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = BUFFER_COUNT - 1;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = sizeof(display_data);
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    STEP("initialize libgxm", sceGxmInitialize(&init));

    int error;
    SceGxmContextParams context = { 0 };
    context.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    context.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    context.vdmRingBufferMem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE,
                                         SCE_GXM_MEMORY_ATTRIB_READ, &error);
    STEP("allocate the VDM ring buffer", error);
    context.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    context.vertexRingBufferMem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                            SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ, &error);
    STEP("allocate the vertex ring buffer", error);
    context.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    context.fragmentRingBufferMem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                              SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ,
                                              &error);
    STEP("allocate the fragment ring buffer", error);
    context.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    context.fragmentUsseRingBufferMem = alloc_block(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                                    SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &error);
    STEP("allocate the fragment USSE ring buffer", error);
    context.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    STEP("map the fragment USSE ring buffer",
         sceGxmMapFragmentUsseMemory(context.fragmentUsseRingBufferMem, SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE,
                                     &context.fragmentUsseRingBufferOffset));
    STEP("create a context", sceGxmCreateContext(&context, &render_context));

    SceGxmRenderTargetParams target_params = { 0 };
    target_params.width = RENDER_WIDTH;
    target_params.height = RENDER_HEIGHT;
    target_params.scenesPerFrame = 1;
    target_params.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    target_params.driverMemBlock = -1;
    STEP("create a render target", sceGxmCreateRenderTarget(&target_params, &target));

    for (int i = 0; i < BUFFER_COUNT; i++) {
        color_data[i] = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, STRIDE * RENDER_HEIGHT * 4,
                                  SCE_GXM_MEMORY_ATTRIB_RW, &error);
        STEP("allocate a frame buffer", error);
        sceClibMemset(color_data[i], 0, STRIDE * RENDER_HEIGHT * 4);
        STEP("set up a color surface",
             sceGxmColorSurfaceInit(&color_surfaces[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
                                    SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, RENDER_WIDTH,
                                    RENDER_HEIGHT, STRIDE, color_data[i]));
        STEP("create a sync object", sceGxmSyncObjectCreate(&syncs[i]));
    }

    void *depth_data = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, DEPTH_STRIDE * DEPTH_HEIGHT * 4,
                                 SCE_GXM_MEMORY_ATTRIB_RW, &error);
    STEP("allocate the depth buffer", error);
    STEP("set up the depth and stencil surface",
         sceGxmDepthStencilSurfaceInit(&depth_surface, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
                                       SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, DEPTH_STRIDE, depth_data, NULL));
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&depth_surface, 1.0f);
    sceGxmDepthStencilSurfaceSetBackgroundStencil(&depth_surface, 0);

    SceGxmShaderPatcherParams patcher_params = { 0 };
    patcher_params.hostAllocCallback = patcher_host_alloc;
    patcher_params.hostFreeCallback = patcher_host_free;
    patcher_params.bufferMem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_MEM_SIZE,
                                         SCE_GXM_MEMORY_ATTRIB_RW, &error);
    STEP("allocate the shader patcher buffer", error);
    patcher_params.bufferMemSize = PATCHER_MEM_SIZE;
    patcher_params.vertexUsseMem = alloc_block(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_MEM_SIZE, &error);
    STEP("allocate the vertex USSE memory", error);
    STEP("map the vertex USSE memory", sceGxmMapVertexUsseMemory(patcher_params.vertexUsseMem, PATCHER_MEM_SIZE,
                                                                 &patcher_params.vertexUsseOffset));
    patcher_params.vertexUsseMemSize = PATCHER_MEM_SIZE;
    patcher_params.fragmentUsseMem = alloc_block(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_MEM_SIZE, &error);
    STEP("allocate the fragment USSE memory", error);
    STEP("map the fragment USSE memory", sceGxmMapFragmentUsseMemory(patcher_params.fragmentUsseMem,
                                                                     PATCHER_MEM_SIZE,
                                                                     &patcher_params.fragmentUsseOffset));
    patcher_params.fragmentUsseMemSize = PATCHER_MEM_SIZE;
    STEP("create the shader patcher", sceGxmShaderPatcherCreate(&patcher_params, &patcher));

    STEP("register clear_v", register_program(clear_v_gxp, &clear_v_id));
    STEP("register clear_f", register_program(clear_f_gxp, &clear_f_id));
    STEP("register color_v", register_program(color_v_gxp, &color_v_id));
    STEP("register color_f", register_program(color_f_gxp, &color_f_id));
    STEP("register texture_v", register_program(texture_v_gxp, &texture_v_id));
    STEP("register texture_f", register_program(texture_f_gxp, &texture_f_id));

    SceGxmVertexAttribute attrs[2];
    SceGxmVertexStream stream = { 0, SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
    STEP("find aPosition of clear_v", attribute(clear_v_gxp, "aPosition", 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, &attrs[0]));
    stream.stride = 2 * sizeof(float);
    STEP("create the clear vertex program",
         sceGxmShaderPatcherCreateVertexProgram(patcher, clear_v_id, attrs, 1, &stream, 1, &clear_vertex));
    STEP("find aPosition of color_v", attribute(color_v_gxp, "aPosition", 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, &attrs[0]));
    STEP("find aColor of color_v", attribute(color_v_gxp, "aColor", 12, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, &attrs[1]));
    stream.stride = sizeof(render_color_vertex);
    STEP("create the color vertex program",
         sceGxmShaderPatcherCreateVertexProgram(patcher, color_v_id, attrs, 2, &stream, 1, &color_vertex));
    STEP("find aPosition of texture_v",
         attribute(texture_v_gxp, "aPosition", 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, &attrs[0]));
    STEP("find aTexcoord of texture_v",
         attribute(texture_v_gxp, "aTexcoord", 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, &attrs[1]));
    stream.stride = sizeof(render_texture_vertex);
    STEP("create the texture vertex program",
         sceGxmShaderPatcherCreateVertexProgram(patcher, texture_v_id, attrs, 2, &stream, 1, &texture_vertex));
    STEP("create the clear fragment program",
         sceGxmShaderPatcherCreateFragmentProgram(patcher, clear_f_id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                  SCE_GXM_MULTISAMPLE_NONE, NULL, clear_v_gxp, &clear_fragment));
    STEP("find uClearColor of clear_f", find_param(clear_f_gxp, "uClearColor", &clear_color_param));
    STEP("find wvp of color_v", find_param(color_v_gxp, "wvp", &color_wvp_param));
    STEP("find wvp of texture_v", find_param(texture_v_gxp, "wvp", &texture_wvp_param));

    indices = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, RENDER_MAX_VERTICES * sizeof(*indices),
                        SCE_GXM_MEMORY_ATTRIB_READ, &error);
    STEP("allocate the index buffer", error);
    for (int i = 0; i < RENDER_MAX_VERTICES; i++)
        indices[i] = i;
    clear_vertices = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 8 * sizeof(float), SCE_GXM_MEMORY_ATTRIB_READ,
                               &error);
    STEP("allocate the clear vertices", error);
    static const float corners[8] = { -1, 1, 1, 1, -1, -1, 1, -1 };
    sceClibMemcpy(clear_vertices, corners, sizeof(corners));
    vertex_mem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, VERTEX_MEM_SIZE, SCE_GXM_MEMORY_ATTRIB_READ, &error);
    STEP("allocate the vertex memory", error);
    texture_mem = alloc_gpu(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, TEXTURE_MEM_SIZE, SCE_GXM_MEMORY_ATTRIB_READ,
                            &error);
    STEP("allocate the texture memory", error);
    return 0;
}

static SceGxmFragmentProgram *fragment_program(SceGxmShaderPatcherId id, const SceGxmProgram *vertex,
                                               const SceGxmBlendInfo *blend) {
    SceGxmFragmentProgram *program = NULL;
    check(sceGxmShaderPatcherCreateFragmentProgram(patcher, id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, blend, vertex, &program));
    return program;
}

SceGxmFragmentProgram *render_color_program(const SceGxmBlendInfo *blend) {
    return fragment_program(color_f_id, color_v_gxp, blend);
}

SceGxmFragmentProgram *render_texture_program(const SceGxmBlendInfo *blend) {
    return fragment_program(texture_f_id, texture_v_gxp, blend);
}

void *render_texture_memory(unsigned int size) {
    size = (size + 255) & ~255u;
    if (texture_mem == NULL || texture_used + size > TEXTURE_MEM_SIZE) {
        check(-1);
        return NULL;
    }
    void *mem = texture_mem + texture_used;
    texture_used += size;
    return mem;
}

// Vertex memory for the frame, which the GPU is done with once the frame before has ended
static void *vertex_memory(unsigned int size) {
    size = (size + 15) & ~15u;
    if (vertex_mem == NULL || vertex_used + size > VERTEX_MEM_SIZE) {
        check(-1);
        return NULL;
    }
    void *mem = vertex_mem + vertex_used;
    vertex_used += size;
    return mem;
}

int render_begin(void) {
    vertex_used = 0;
    const int result = sceGxmBeginScene(render_context, 0, target, NULL, NULL, syncs[back], &color_surfaces[back],
                                        &depth_surface);
    check(result);
    sceGxmSetViewport(render_context, RENDER_WIDTH / 2.0f, RENDER_WIDTH / 2.0f, RENDER_HEIGHT / 2.0f,
                      -RENDER_HEIGHT / 2.0f, 0.0f, 1.0f);
    sceGxmSetCullMode(render_context, SCE_GXM_CULL_NONE);
    sceGxmSetTwoSidedEnable(render_context, SCE_GXM_TWO_SIDED_DISABLED);
    sceGxmSetFrontDepthFunc(render_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(render_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(render_context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(render_context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetFrontStencilFunc(render_context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
                              SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
    sceGxmSetBackStencilFunc(render_context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
                             SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
    sceGxmSetFrontStencilRef(render_context, 0);
    sceGxmSetBackStencilRef(render_context, 0);
    return result;
}

void render_clear(unsigned int color) {
    sceGxmSetVertexProgram(render_context, clear_vertex);
    sceGxmSetFragmentProgram(render_context, clear_fragment);
    void *uniforms;
    check(sceGxmReserveFragmentDefaultUniformBuffer(render_context, &uniforms));
    const float rgba[4] = { (color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f,
                            (color >> 24) / 255.0f };
    check(sceGxmSetUniformDataF(uniforms, clear_color_param, 0, 4, rgba));
    check(sceGxmSetVertexStream(render_context, 0, clear_vertices));
    check(sceGxmDraw(render_context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4));
}

// Draws count vertices of size bytes each, copied to vertex memory, with the vertex program and wvp
static void draw(SceGxmVertexProgram *vertex, const SceGxmProgramParameter *wvp_param,
                 SceGxmFragmentProgram *fragment, SceGxmPrimitiveType type, const void *vertices, int count, int size) {
    if (fragment == NULL || count <= 0 || count > RENDER_MAX_VERTICES) {
        check(-1);
        return;
    }
    void *data = vertex_memory(count * size);
    if (data == NULL)
        return;
    sceClibMemcpy(data, vertices, count * size);
    sceGxmSetVertexProgram(render_context, vertex);
    sceGxmSetFragmentProgram(render_context, fragment);
    void *uniforms;
    check(sceGxmReserveVertexDefaultUniformBuffer(render_context, &uniforms));
    check(sceGxmSetUniformDataF(uniforms, wvp_param, 0, 16, wvp));
    check(sceGxmSetVertexStream(render_context, 0, data));
    check(sceGxmDraw(render_context, type, SCE_GXM_INDEX_FORMAT_U16, indices, count));
}

void render_colored(SceGxmFragmentProgram *program, SceGxmPrimitiveType type, const render_color_vertex *vertices,
                    int count) {
    draw(color_vertex, color_wvp_param, program, type, vertices, count, sizeof(*vertices));
}

void render_rect(SceGxmFragmentProgram *program, float x0, float y0, float x1, float y1, float z, unsigned int color) {
    const render_color_vertex vertices[4] = {
        { x0, y0, z, color },
        { x1, y0, z, color },
        { x0, y1, z, color },
        { x1, y1, z, color },
    };
    render_colored(program, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, vertices, 4);
}

void render_textured(SceGxmFragmentProgram *program, const SceGxmTexture *texture, SceGxmPrimitiveType type,
                     const render_texture_vertex *vertices, int count) {
    check(sceGxmSetFragmentTexture(render_context, 0, texture));
    draw(texture_vertex, texture_wvp_param, program, type, vertices, count, sizeof(*vertices));
}

void render_textured_rect(SceGxmFragmentProgram *program, const SceGxmTexture *texture, float x0, float y0, float x1,
                          float y1, float u0, float v0, float u1, float v1) {
    const render_texture_vertex vertices[4] = {
        { x0, y0, 0.5f, u0, v0 },
        { x1, y0, 0.5f, u1, v0 },
        { x0, y1, 0.5f, u0, v1 },
        { x1, y1, 0.5f, u1, v1 },
    };
    render_textured(program, texture, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, vertices, 4);
}

int render_end(void) {
    check(sceGxmEndScene(render_context, NULL, NULL));
    const display_data data = { color_data[back] };
    check(sceGxmDisplayQueueAddEntry(syncs[back ^ 1], syncs[back], &data));
    // Until the display shows this frame
    check(sceGxmDisplayQueueFinish());
    sceGxmFinish(render_context);
    back ^= 1;
    const int result = render_error;
    render_error = 0;
    return result;
}
