// How many bytes of a deferred context's VDM, vertex and fragment memory libgxm uses for textures,
// uniform buffers, uniform data and program changes, measured like gxm_deferred_ring_usage: each
// case is one command list on fresh 64 KiB buffers set with the deferred buffer setters, its VDM
// bytes are the end's terminator address minus its start address (-1 when those words don't point
// into the VDM buffer), its vertex and fragment bytes are the getters' heads after the end minus
// the buffers' starts (-1 when a head isn't in its buffer), and it runs with 1, 2 and 3 repeats.
// Each list starts by setting the same value of every state a case changes. Two program pairs draw:
// a textured pair with the vertex default uniform wvp and the sampler tex, and a clear pair with
// the fragment default uniform uClearColor and no vertex uniform. The textured draws reserve the
// vertex default uniform buffer first, the clear draws the fragment one. The change cases draw
// once, then change one thing and draw again per repeat. The output has every call's first error,
// the byte counts and the callback count.
//
// On a PS Vita TV on firmware 3.65 no call fails and no case calls a memory callback. A textured
// draw takes what the plain draw of gxm_deferred_ring_usage takes plus 0x14 fragment bytes for the
// texture: 0x28 VDM, 0x154 vertex and 0x70 fragment bytes for the first one, 0x20 VDM bytes for
// each after it. A texture change and a draw take 0x28 VDM, 0xB0 vertex and 0x30 fragment bytes.
// Setting the same texture again adds nothing. A set vertex default uniform buffer needs no reserve
// and takes no vertex bytes for the uniforms. A clear draw takes 0x28 VDM, 0x100 vertex and 0x68
// fragment bytes first, then 0x20 VDM bytes and about 0x40 vertex and fragment bytes each. Writing
// uniform data and changing a uniform buffer the programs don't use add nothing. A fragment program
// enable change costs nothing over a plain draw, and a draw with the fragment program disabled
// takes fewer fragment bytes. A draw with the other program pair takes 0x28 VDM bytes.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "gxm_deferred_ring_usage_more_programs.h"
#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE
#define RING_SIZE (64 * KIB)
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define REGION_SIZE (64 * KIB)
#define TEXTURE_SIZE 16

// The block's layout: the three rings, the callback memory, the vertex data, two textures and two
// uniform buffers
#define VDM_RING 0
#define VERTEX_RING (64 * KIB)
#define FRAGMENT_RING (128 * KIB)
#define CALLBACK_MEM (256 * KIB)
#define VERTEX_DATA MIB
#define TEXTURE_DATA (MIB + 64 * KIB)
#define UNIFORM_DATA (MIB + 128 * KIB)

int sceGxmGetDeferredContextVertexBuffer(SceGxmContext *context, void **mem);
int sceGxmGetDeferredContextFragmentBuffer(SceGxmContext *context, void **mem);
int sceGxmSetDeferredContextVdmBuffer(SceGxmContext *context, void *mem, unsigned int size);
int sceGxmSetDeferredContextVertexBuffer(SceGxmContext *context, void *mem, unsigned int size);
int sceGxmSetDeferredContextFragmentBuffer(SceGxmContext *context, void *mem, unsigned int size);

static SceUID block;
static char *base;
static SceGxmContext *context;
static char host_mem[HOST_SIZE + 64] __attribute__((aligned(16)));
static SceGxmVertexProgram *texture_vertex, *clear_vertex;
static SceGxmFragmentProgram *texture_fragment, *clear_fragment;
static const SceGxmProgramParameter *clear_color;
static SceGxmTexture textures[2];
static const unsigned short *indices;

static volatile int callbacks;
static int first_error;

static char host_pool[REGION_SIZE] __attribute__((aligned(16)));
static unsigned int host_used;

static void *memory_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    (void)requested;
    callbacks++;
    *size = RING_SIZE;
    return base + CALLBACK_MEM;
}

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

static void note(int result) {
    if (result < 0 && first_error == 0)
        first_error = result;
}

// The VDM bytes of a list in the buffer at start, or -1 when its words don't point into that buffer
static int vdm_bytes(const SceGxmCommandList *list, const char *start, unsigned int size) {
    const unsigned int first = (unsigned int)start, last = first + size;
    if (list->words[0] < first || list->words[1] < list->words[0] || list->words[1] >= last)
        return -1;
    return list->words[1] - list->words[0];
}

// The bytes before head in the buffer at start, or -1 when head isn't in that buffer
static int ring_bytes(const void *head, const char *start, unsigned int size) {
    const char *at = head;
    if (at < start || at > start + size)
        return -1;
    return at - start;
}

static void set_rings(void) {
    note(sceGxmSetDeferredContextVdmBuffer(context, base + VDM_RING, RING_SIZE));
    note(sceGxmSetDeferredContextVertexBuffer(context, base + VERTEX_RING, RING_SIZE));
    note(sceGxmSetDeferredContextFragmentBuffer(context, base + FRAGMENT_RING, RING_SIZE));
}

// The same value of every state a case changes, set at the start of each list
static void set_baseline(void) {
    note(sceGxmSetFragmentTexture(context, 0, &textures[0]));
    note(sceGxmSetVertexUniformBuffer(context, 0, base + UNIFORM_DATA));
    note(sceGxmSetFragmentUniformBuffer(context, 0, base + UNIFORM_DATA));
    sceGxmSetFrontFragmentProgramEnable(context, SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
}

static void set_texture_pair(void) {
    sceGxmSetVertexProgram(context, texture_vertex);
    sceGxmSetFragmentProgram(context, texture_fragment);
    note(sceGxmSetVertexStream(context, 0, base + VERTEX_DATA));
}

static void set_clear_pair(void) {
    sceGxmSetVertexProgram(context, clear_vertex);
    sceGxmSetFragmentProgram(context, clear_fragment);
    note(sceGxmSetVertexStream(context, 0, base + VERTEX_DATA));
}

static void draw(void) {
    note(sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, indices, 3));
}

static void textured_draw(void) {
    void *uniforms;
    note(sceGxmReserveVertexDefaultUniformBuffer(context, &uniforms));
    draw();
}

static void clear_draw(void) {
    void *uniforms;
    note(sceGxmReserveFragmentDefaultUniformBuffer(context, &uniforms));
    draw();
}

static void case_textured(int i) {
    if (i == 0)
        set_texture_pair();
    textured_draw();
}

static void case_textured_change(int i) {
    if (i == 0) {
        set_texture_pair();
        textured_draw();
    }
    note(sceGxmSetFragmentTexture(context, 0, &textures[(i + 1) & 1]));
    textured_draw();
}

static void case_textured_same(int i) {
    if (i == 0) {
        set_texture_pair();
        textured_draw();
    }
    note(sceGxmSetFragmentTexture(context, 0, &textures[0]));
    textured_draw();
}

static void case_textured_set_default(int i) {
    if (i == 0)
        set_texture_pair();
    note(sceGxmSetVertexDefaultUniformBuffer(context, base + UNIFORM_DATA + 256 * i));
    draw();
}

static void case_clear(int i) {
    if (i == 0)
        set_clear_pair();
    clear_draw();
}

static void case_clear_uniform_data(int i) {
    if (i == 0)
        set_clear_pair();
    void *uniforms = NULL;
    note(sceGxmReserveFragmentDefaultUniformBuffer(context, &uniforms));
    const float color[4] = { 0.25f * i, 0.5f, 0.75f, 1.0f };
    note(sceGxmSetUniformDataF(uniforms, clear_color, 0, 4, color));
    draw();
}

static void case_clear_vertex_buffer(int i) {
    if (i == 0) {
        set_clear_pair();
        clear_draw();
    }
    note(sceGxmSetVertexUniformBuffer(context, 0, base + UNIFORM_DATA + 256 * (i + 1)));
    clear_draw();
}

static void case_clear_fragment_buffer(int i) {
    if (i == 0) {
        set_clear_pair();
        clear_draw();
    }
    note(sceGxmSetFragmentUniformBuffer(context, 0, base + UNIFORM_DATA + 256 * (i + 1)));
    clear_draw();
}

static void case_clear_fragment_disable(int i) {
    if (i == 0) {
        set_clear_pair();
        clear_draw();
    }
    sceGxmSetFrontFragmentProgramEnable(context, i & 1 ? SCE_GXM_FRAGMENT_PROGRAM_ENABLED : SCE_GXM_FRAGMENT_PROGRAM_DISABLED);
    clear_draw();
}

// A clear draw, then per repeat a draw with the other pair
static void case_pair_switch(int i) {
    if (i == 0) {
        set_clear_pair();
        clear_draw();
    }
    if (i & 1) {
        set_clear_pair();
        clear_draw();
    } else {
        set_texture_pair();
        textured_draw();
    }
}

struct ring_case {
    const char *name;
    void (*calls)(int i);
};

static const struct ring_case cases[] = {
    { "textured draws", case_textured },
    { "textured draws with a texture change", case_textured_change },
    { "textured draws with the same texture set again", case_textured_same },
    { "textured draws with a set vertex default uniform buffer", case_textured_set_default },
    { "clear draws", case_clear },
    { "clear draws with uniform data written", case_clear_uniform_data },
    { "clear draws with a vertex uniform buffer change", case_clear_vertex_buffer },
    { "clear draws with a fragment uniform buffer change", case_clear_fragment_buffer },
    { "clear draws with a fragment program enable change", case_clear_fragment_disable },
    { "draws switching between the program pairs", case_pair_switch },
};

static void run_case(const struct ring_case *c) {
    for (int repeats = 1; repeats <= 3; repeats++) {
        first_error = 0;
        set_rings();
        note(sceGxmBeginCommandList(context));
        set_baseline();
        for (int i = 0; i < repeats; i++)
            c->calls(i);
        SceGxmCommandList list = { 0 };
        note(sceGxmEndCommandList(context, &list));
        void *vertex_head = NULL, *fragment_head = NULL;
        note(sceGxmGetDeferredContextVertexBuffer(context, &vertex_head));
        note(sceGxmGetDeferredContextFragmentBuffer(context, &fragment_head));
        PRINTF(first_error, "%s x%d: first error", c->name, repeats);
        PRINTF(vdm_bytes(&list, base + VDM_RING, RING_SIZE), "%s x%d: VDM bytes", c->name, repeats);
        PRINTF(ring_bytes(vertex_head, base + VERTEX_RING, RING_SIZE), "%s x%d: vertex bytes", c->name, repeats);
        PRINTF(ring_bytes(fragment_head, base + FRAGMENT_RING, RING_SIZE), "%s x%d: fragment bytes", c->name, repeats);
    }
}

static int make_vertex_program(SceGxmShaderPatcher *patcher, SceGxmShaderPatcherId id, int texcoord, SceGxmVertexProgram **program) {
    const SceGxmProgram *gxp = sceGxmShaderPatcherGetProgramFromId(id);
    SceGxmVertexAttribute attributes[2] = {
        { .streamIndex = 0, .offset = 0, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32, .componentCount = 3 },
        { .streamIndex = 0, .offset = 12, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32, .componentCount = 2 },
    };
    attributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(gxp, "aPosition"));
    if (texcoord)
        attributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(gxp, "aTexcoord"));
    // A position of 3 floats and a texture coordinate of 2, 20 bytes per vertex
    const SceGxmVertexStream stream = { .stride = 20, .indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
    return sceGxmShaderPatcherCreateVertexProgram(patcher, id, attributes, texcoord ? 2 : 1, &stream, 1, program);
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));

    block = sceKernelAllocMemBlock("ring usage", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 2 * MIB, NULL);
    PRINT("allocate the block", test_created(block));
    sceKernelGetMemBlockBase(block, (void **)&base);
    PRINT("map the block", sceGxmMapMemory(base, 2 * MIB, RW));

    SceGxmDeferredContextParams params = { 0 };
    params.hostMem = host_mem;
    params.hostMemSize = HOST_SIZE;
    params.vdmCallback = memory_callback;
    params.vertexCallback = memory_callback;
    params.fragmentCallback = memory_callback;
    PRINT("create the deferred context", sceGxmCreateDeferredContext(&params, &context));

    // The patcher's buffer, vertex USSE and fragment USSE regions, each in a block of its own
    SceUID region_block[3];
    char *region_base[3];
    for (int i = 0; i < 3; i++) {
        region_block[i] = sceKernelAllocMemBlock("ring usage", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, REGION_SIZE, NULL);
        sceKernelGetMemBlockBase(region_block[i], (void **)&region_base[i]);
    }
    unsigned int vertex_usse_offset = 0, fragment_usse_offset = 0;
    sceGxmMapMemory(region_base[0], REGION_SIZE, RW);
    sceGxmMapVertexUsseMemory(region_base[1], REGION_SIZE, &vertex_usse_offset);
    sceGxmMapFragmentUsseMemory(region_base[2], REGION_SIZE, &fragment_usse_offset);

    SceGxmShaderPatcherParams patcher_params = { 0 };
    patcher_params.hostAllocCallback = host_alloc;
    patcher_params.hostFreeCallback = host_free;
    patcher_params.bufferMem = region_base[0];
    patcher_params.bufferMemSize = REGION_SIZE;
    patcher_params.vertexUsseMem = region_base[1];
    patcher_params.vertexUsseMemSize = REGION_SIZE;
    patcher_params.vertexUsseOffset = vertex_usse_offset;
    patcher_params.fragmentUsseMem = region_base[2];
    patcher_params.fragmentUsseMemSize = REGION_SIZE;
    patcher_params.fragmentUsseOffset = fragment_usse_offset;
    SceGxmShaderPatcher *patcher = NULL;
    PRINT("create the shader patcher", sceGxmShaderPatcherCreate(&patcher_params, &patcher));

    SceGxmShaderPatcherId ids[4] = { NULL };
    const unsigned char *gxps[4] = { texture_v_gxp, texture_f_gxp, clear_v_gxp, clear_f_gxp };
    int registered = 0;
    for (int i = 0; i < 4; i++)
        registered |= sceGxmShaderPatcherRegisterProgram(patcher, (const SceGxmProgram *)gxps[i], &ids[i]);
    PRINT("register the programs", registered);
    PRINT("create the textured vertex program", make_vertex_program(patcher, ids[0], 1, &texture_vertex));
    PRINT("create the clear vertex program", make_vertex_program(patcher, ids[2], 0, &clear_vertex));
    PRINT("create the textured fragment program", sceGxmShaderPatcherCreateFragmentProgram(patcher, ids[1],
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL, sceGxmShaderPatcherGetProgramFromId(ids[0]), &texture_fragment));
    PRINT("create the clear fragment program", sceGxmShaderPatcherCreateFragmentProgram(patcher, ids[3],
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL, sceGxmShaderPatcherGetProgramFromId(ids[2]), &clear_fragment));
    clear_color = sceGxmProgramFindParameterByName(sceGxmShaderPatcherGetProgramFromId(ids[3]), "uClearColor");
    PRINT("find uClearColor", clear_color != NULL);

    // One triangle, the three indices after it, and two 16x16 textures
    float *vertices = (float *)(base + VERTEX_DATA);
    for (int i = 0; i < 15; i++)
        vertices[i] = (float)(i % 3);
    unsigned short *index_data = (unsigned short *)(base + VERTEX_DATA + 256);
    for (int i = 0; i < 3; i++)
        index_data[i] = i;
    indices = index_data;
    for (int i = 0; i < 2; i++)
        PRINTF(sceGxmTextureInitLinear(&textures[i], base + TEXTURE_DATA + i * 4 * KIB, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
            TEXTURE_SIZE, TEXTURE_SIZE, 0), "init texture %d", i);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);
    PRINT("memory callbacks", callbacks);

    PRINT("destroy the deferred context", sceGxmDestroyDeferredContext(context));
    int released = sceGxmShaderPatcherReleaseFragmentProgram(patcher, texture_fragment);
    released |= sceGxmShaderPatcherReleaseFragmentProgram(patcher, clear_fragment);
    released |= sceGxmShaderPatcherReleaseVertexProgram(patcher, texture_vertex);
    released |= sceGxmShaderPatcherReleaseVertexProgram(patcher, clear_vertex);
    for (int i = 0; i < 4; i++)
        released |= sceGxmShaderPatcherUnregisterProgram(patcher, ids[i]);
    PRINT("release and unregister the programs", released);
    PRINT("destroy the shader patcher", sceGxmShaderPatcherDestroy(patcher));
    sceGxmUnmapMemory(region_base[0]);
    sceGxmUnmapVertexUsseMemory(region_base[1]);
    sceGxmUnmapFragmentUsseMemory(region_base[2]);
    for (int i = 0; i < 3; i++)
        sceKernelFreeMemBlock(region_block[i]);
    sceGxmUnmapMemory(base);
    sceKernelFreeMemBlock(block);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
