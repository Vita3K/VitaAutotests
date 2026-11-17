// Where the GPU writes the rows of a linear color surface whose stride is above 32768 pixels.
// sceGxmColorSurfaceInit takes a linear stride of up to 131072 bytes, but libgxm stores it in the
// surface as ((strideInPixels + 1) / 2 - 1) & 0x3fff, 14 bits that count pairs of pixels.
//
// Each case is a linear surface of 32 by 4 pixels, U8_R or U8U8_GR, with the 32 bit output
// register size, at the base of a 1 MiB USER_RW_UNCACHE block of its own mapped with READ and
// WRITE. The strides are 32768 pixels, where the wrapped and the full pitch agree, 32770, whose
// 32770 bytes aren't a multiple of 4, 32772, 32832, 40000, 65536, 131072, the most bytes init
// takes, and 131076, one 4 byte step more, all in U8_R, then 40000 in U8U8_GR. The test fills the
// block with 0x5A, then one scene draws each row with a rectangle of its own color, 0x11, 0x22,
// 0x33 and 0x44 in every channel, in row order, and sceGxmFinish waits for it. For each case it
// prints the init result, the stride sceGxmColorSurfaceGetStrideInPixels gives, the byte offset of
// the first byte of each row's color in the block (-1 when there is none), the number of bytes that
// changed, and whether the block holds exactly what the wrapped pitch or the full stride gives,
// with later rows written over earlier ones where they overlap. A case whose init fails prints only
// the init result.
//
// On a PS Vita TV on firmware 3.65 a wider stride wraps: 65536 and 131072 give a row pitch of 32768
// pixels, 40000 gives 7232, 32772 gives 4 and 32832 gives 64, and the GPU writes the rows at that
// pitch. sceGxmColorSurfaceGetStrideInPixels returns the wrapped pitch too. 32770 fails with
// 0x805B0005 and 131076 with 0x805B0003.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "gxm_deferred_ring_usage_more_programs.h"
#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define SURFACE_SIZE MIB
#define REGION_SIZE (64 * KIB)
#define WIDTH 32
#define HEIGHT 4
#define SENTINEL 0x5A

// sceGxmFinish returns void in the headers, this is the same function with its result
int gxm_finish(SceGxmContext *context) __asm__("sceGxmFinish");

static char host_mem[HOST_SIZE + 64] __attribute__((aligned(16)));
static char host_pool[REGION_SIZE] __attribute__((aligned(16)));
static unsigned int host_used;

static SceGxmContext *context;
static SceGxmRenderTarget *target;
static SceGxmVertexProgram *vertex_program;
static SceGxmFragmentProgram *fragment_program;
static const SceGxmProgramParameter *clear_color;
static unsigned char *surface_mem;

struct stride_case {
    const char *format_name;
    SceGxmColorFormat format;
    unsigned int bytes_per_pixel, stride;
};

static const struct stride_case cases[] = {
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 32768 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 32770 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 32772 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 32832 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 40000 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 65536 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 131072 },
    { "U8_R", SCE_GXM_COLOR_FORMAT_U8_R, 1, 131076 },
    { "U8U8_GR", SCE_GXM_COLOR_FORMAT_U8U8_GR, 2, 40000 },
};

#define CASES (sizeof(cases) / sizeof(cases[0]))
static const unsigned short *indices;
static const float *vertices;

// A copy of the block after the scene, and what it should hold, built for each case
static unsigned char seen[SURFACE_SIZE], model[SURFACE_SIZE];

static const unsigned char row_value[HEIGHT] = { 0x11, 0x22, 0x33, 0x44 };

static void display_callback(const void *data) {
    (void)data;
}

static void *host_alloc(void *user_data, SceSize size) {
    (void)user_data;
    const unsigned int start = host_used;
    host_used += (size + 15) & ~15u;
    return host_used <= sizeof(host_pool) ? host_pool + start : NULL;
}

static void host_free(void *user_data, void *mem) {
    (void)user_data;
    (void)mem;
}

static char *alloc(SceUID *uid, SceSize size) {
    *uid = sceKernelAllocMemBlock("gxm wide stride", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
                                  NULL);
    void *base = NULL;
    if (*uid >= 0)
        sceKernelGetMemBlockBase(*uid, &base);
    return base;
}

// Fills the model with the rows written pitch bytes apart, in row order
static void build_model(unsigned int pitch, unsigned int bytes_per_pixel) {
    sceClibMemset(model, SENTINEL, SURFACE_SIZE);
    for (int row = 0; row < HEIGHT; row++) {
        const unsigned int start = row * pitch;
        if (start + WIDTH * bytes_per_pixel <= SURFACE_SIZE)
            sceClibMemset(model + start, row_value[row], WIDTH * bytes_per_pixel);
    }
}

static int matches_model(void) {
    return sceClibMemcmp(model, seen, SURFACE_SIZE) == 0;
}

// The offset of the first byte with the value, or -1
static int first_offset(unsigned char value) {
    for (int i = 0; i < SURFACE_SIZE; i++)
        if (seen[i] == value)
            return i;
    return -1;
}

static int changed_bytes(void) {
    int count = 0;
    for (int i = 0; i < SURFACE_SIZE; i++)
        if (seen[i] != SENTINEL)
            count++;
    return count;
}

// One scene that draws every row with its own color
static int draw_rows(SceGxmColorSurface *surface) {
    int result = sceGxmBeginScene(context, 0, target, NULL, NULL, NULL, surface, NULL);
    if (result < 0)
        return result;
    sceGxmSetVertexProgram(context, vertex_program);
    sceGxmSetFragmentProgram(context, fragment_program);
    sceGxmSetFrontDepthFunc(context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetCullMode(context, SCE_GXM_CULL_NONE);
    // Maps x from -1 to 1 onto the 32 columns and y from -1 to 1 onto the 4 rows, top row first
    sceGxmSetViewport(context, WIDTH / 2.0f, WIDTH / 2.0f, HEIGHT / 2.0f, HEIGHT / 2.0f, 0.5f,
                      0.5f);
    for (int row = 0; row < HEIGHT && result == 0; row++) {
        void *uniforms = NULL;
        result = sceGxmReserveFragmentDefaultUniformBuffer(context, &uniforms);
        if (result < 0)
            break;
        const float value = row_value[row] / 255.0f;
        const float color[4] = { value, value, value, value };
        sceGxmSetUniformDataF(uniforms, clear_color, 0, 4, color);
        result = sceGxmSetVertexStream(context, 0, vertices);
        if (result == 0)
            result = sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16,
                                indices + 6 * row, 6);
    }
    const int ended = sceGxmEndScene(context, NULL, NULL);
    if (result == 0)
        result = ended;
    const int finished = gxm_finish(context);
    return result < 0 ? result : finished;
}

static void run_case(const char *format_name, SceGxmColorFormat format,
                     unsigned int bytes_per_pixel, unsigned int stride) {
    SceGxmColorSurface surface;
    sceClibMemset(&surface, 0, sizeof(surface));
    const int init = sceGxmColorSurfaceInit(&surface, format, SCE_GXM_COLOR_SURFACE_LINEAR,
                                            SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                                            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, WIDTH, HEIGHT,
                                            stride, surface_mem);
    PRINTF(init, "%s stride %u: init", format_name, stride);
    if (init < 0)
        return;
    PRINTF(sceGxmColorSurfaceGetStrideInPixels(&surface), "%s stride %u: stride it reports",
           format_name, stride);

    sceClibMemset(surface_mem, SENTINEL, SURFACE_SIZE);
    PRINTF(draw_rows(&surface), "%s stride %u: draw and finish", format_name, stride);
    sceClibMemcpy(seen, surface_mem, SURFACE_SIZE);
    for (int row = 0; row < HEIGHT; row++)
        PRINTF(first_offset(row_value[row]), "%s stride %u: row %d starts at byte", format_name,
               stride, row);
    PRINTF(changed_bytes(), "%s stride %u: bytes changed", format_name, stride);

    const unsigned int wrapped = (((stride + 1) / 2 - 1) & 0x3fff) * 2 + 2;
    build_model(wrapped * bytes_per_pixel, bytes_per_pixel);
    PRINTF(matches_model(), "%s stride %u: matches the wrapped pitch of %u pixels", format_name,
           stride, wrapped);
    build_model(stride * bytes_per_pixel, bytes_per_pixel);
    PRINTF(matches_model(), "%s stride %u: matches the full stride", format_name, stride);
}

static int make_programs(SceGxmShaderPatcher *patcher, SceGxmShaderPatcherId ids[2]) {
    int result = sceGxmShaderPatcherRegisterProgram(patcher, (const SceGxmProgram *)clear_v_gxp,
                                                    &ids[0]);
    result |= sceGxmShaderPatcherRegisterProgram(patcher, (const SceGxmProgram *)clear_f_gxp,
                                                 &ids[1]);
    if (result < 0)
        return result;
    const SceGxmProgram *vertex_gxp = sceGxmShaderPatcherGetProgramFromId(ids[0]);
    SceGxmVertexAttribute attribute = { .streamIndex = 0,
                                        .offset = 0,
                                        .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
                                        .componentCount = 3 };
    attribute.regIndex = sceGxmProgramParameterGetResourceIndex(
        sceGxmProgramFindParameterByName(vertex_gxp, "aPosition"));
    // A position of 3 floats, 12 bytes per vertex
    const SceGxmVertexStream stream = { .stride = 12,
                                        .indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
    result = sceGxmShaderPatcherCreateVertexProgram(patcher, ids[0], &attribute, 1, &stream, 1,
                                                    &vertex_program);
    if (result < 0)
        return result;
    result = sceGxmShaderPatcherCreateFragmentProgram(
        patcher, ids[1], SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL,
        vertex_gxp, &fragment_program);
    if (result < 0)
        return result;
    clear_color = sceGxmProgramFindParameterByName(sceGxmShaderPatcherGetProgramFromId(ids[1]),
                                                   "uClearColor");
    return clear_color != NULL ? 0 : -1;
}

// Four corners and two triangles for each row, in clip space
static void write_geometry(char *base) {
    float *v = (float *)base;
    unsigned short *index = (unsigned short *)(base + 4 * KIB);
    for (int row = 0; row < HEIGHT; row++) {
        const float top = -1.0f + 2.0f * row / HEIGHT, bottom = -1.0f + 2.0f * (row + 1) / HEIGHT;
        const float corners[4][2] = { { -1.0f, top }, { 1.0f, top }, { -1.0f, bottom },
                                      { 1.0f, bottom } };
        for (int i = 0; i < 4; i++) {
            v[(row * 4 + i) * 3 + 0] = corners[i][0];
            v[(row * 4 + i) * 3 + 1] = corners[i][1];
            v[(row * 4 + i) * 3 + 2] = 0.5f;
        }
        const unsigned short order[6] = { 0, 1, 2, 1, 3, 2 };
        for (int i = 0; i < 6; i++)
            index[row * 6 + i] = row * 4 + order[i];
    }
    vertices = v;
    indices = index;
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));

    // The ring buffers are 4 MiB mapped with READ, the VDM ring buffer first, then the fragment
    // ring buffer at 256 KiB, the vertex ring buffer at 1 MiB and the geometry at 3 MiB
    SceUID rings, usse[3];
    char *const rings_base = alloc(&rings, 4 * MIB);
    sceGxmMapMemory(rings_base, 4 * MIB, SCE_GXM_MEMORY_ATTRIB_READ);
    char *usse_base[3];
    for (int i = 0; i < 3; i++)
        usse_base[i] = alloc(&usse[i], REGION_SIZE);
    unsigned int vertex_usse_offset = 0, fragment_usse_offset = 0, ring_usse_offset = 0;
    sceGxmMapMemory(usse_base[0], REGION_SIZE, RW);
    sceGxmMapVertexUsseMemory(usse_base[1], REGION_SIZE, &vertex_usse_offset);
    sceGxmMapFragmentUsseMemory(usse_base[2], REGION_SIZE, &fragment_usse_offset);
    SceUID ring_usse;
    char *const ring_usse_base = alloc(&ring_usse, REGION_SIZE);
    sceGxmMapFragmentUsseMemory(ring_usse_base, REGION_SIZE, &ring_usse_offset);

    SceGxmContextParams params = { 0 };
    params.hostMem = host_mem;
    params.hostMemSize = HOST_SIZE;
    params.vdmRingBufferMem = rings_base;
    params.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    params.vertexRingBufferMem = rings_base + MIB;
    params.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    params.fragmentRingBufferMem = rings_base + 256 * KIB;
    params.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferMem = ring_usse_base;
    params.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferOffset = ring_usse_offset;
    PRINT("create the context", sceGxmCreateContext(&params, &context));

    SceGxmRenderTargetParams target_params = { 0 };
    target_params.width = WIDTH;
    target_params.height = HEIGHT;
    target_params.scenesPerFrame = 1;
    target_params.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    target_params.driverMemBlock = -1;
    PRINT("create the render target", sceGxmCreateRenderTarget(&target_params, &target));

    SceGxmShaderPatcherParams patcher_params = { 0 };
    patcher_params.hostAllocCallback = host_alloc;
    patcher_params.hostFreeCallback = host_free;
    patcher_params.bufferMem = usse_base[0];
    patcher_params.bufferMemSize = REGION_SIZE;
    patcher_params.vertexUsseMem = usse_base[1];
    patcher_params.vertexUsseMemSize = REGION_SIZE;
    patcher_params.vertexUsseOffset = vertex_usse_offset;
    patcher_params.fragmentUsseMem = usse_base[2];
    patcher_params.fragmentUsseMemSize = REGION_SIZE;
    patcher_params.fragmentUsseOffset = fragment_usse_offset;
    SceGxmShaderPatcher *patcher = NULL;
    PRINT("create the shader patcher", sceGxmShaderPatcherCreate(&patcher_params, &patcher));
    SceGxmShaderPatcherId ids[2] = { NULL, NULL };
    PRINT("make the programs", make_programs(patcher, ids));
    write_geometry(rings_base + 3 * MIB);

    // Each case gets a block of its own, and every block lives until the end, so no two surfaces
    // share an address
    SceUID surface_block[CASES];
    char *surface_base[CASES];
    int allocated = 0, mapped = 0;
    for (unsigned int i = 0; i < CASES; i++) {
        surface_base[i] = alloc(&surface_block[i], SURFACE_SIZE);
        allocated |= test_created(surface_block[i]);
        mapped |= sceGxmMapMemory(surface_base[i], SURFACE_SIZE, RW);
    }
    PRINT("allocate the surface blocks", allocated);
    PRINT("map the surface blocks", mapped);
    for (unsigned int i = 0; i < CASES; i++) {
        surface_mem = (unsigned char *)surface_base[i];
        run_case(cases[i].format_name, cases[i].format, cases[i].bytes_per_pixel, cases[i].stride);
    }

    int released = sceGxmShaderPatcherReleaseFragmentProgram(patcher, fragment_program);
    released |= sceGxmShaderPatcherReleaseVertexProgram(patcher, vertex_program);
    released |= sceGxmShaderPatcherUnregisterProgram(patcher, ids[0]);
    released |= sceGxmShaderPatcherUnregisterProgram(patcher, ids[1]);
    PRINT("release and unregister the programs", released);
    PRINT("destroy the shader patcher", sceGxmShaderPatcherDestroy(patcher));
    PRINT("destroy the render target", sceGxmDestroyRenderTarget(target));
    PRINT("destroy the context", sceGxmDestroyContext(context));
    for (unsigned int i = 0; i < CASES; i++) {
        sceGxmUnmapMemory(surface_base[i]);
        sceKernelFreeMemBlock(surface_block[i]);
    }
    sceGxmUnmapFragmentUsseMemory(ring_usse_base);
    sceKernelFreeMemBlock(ring_usse);
    sceGxmUnmapMemory(usse_base[0]);
    sceGxmUnmapVertexUsseMemory(usse_base[1]);
    sceGxmUnmapFragmentUsseMemory(usse_base[2]);
    for (int i = 0; i < 3; i++)
        sceKernelFreeMemBlock(usse[i]);
    sceGxmUnmapMemory(rings_base);
    sceKernelFreeMemBlock(rings);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
