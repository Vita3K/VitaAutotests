// How many bytes of a deferred context's VDM, vertex and fragment memory libgxm uses for each kind
// of call. Before each case the context gets a fresh 64 KiB buffer for each of the three through
// the deferred buffer setters, so no case needs a memory callback. A case is one command list: the
// begin, some calls, the end. Its VDM bytes are the end's terminator address minus its start
// address, the two first words of the command list the end fills, or -1 when those words don't
// point into the VDM buffer. Its vertex and fragment bytes are what the deferred buffer getters
// return after the end minus the buffer's start, or -1 when that isn't in the buffer. Each case
// runs with 1, 2 and 3 repeats of its calls, so the bytes per repeat show as the steps between
// them. Each list starts by setting the same value of every state a case changes. The draws use a
// vertex program with a default uniform buffer, a fragment program without one and one vertex
// stream, and reserve the vertex default uniform buffer first unless the case says otherwise. The
// state cases draw once, then change one state and draw again per repeat, so the step over the
// plain draw case is what the changed state adds. Then a second list right after a first one shows
// where it starts. Last, a 1 KiB VDM buffer shows how many empty lists and how many draws fit
// before the VDM callback runs, and what size the callback is asked for. The output has every
// call's first error, the byte counts, the command list words that hold no address and the callback
// counts.
//
// On a PS Vita TV on firmware 3.65 no case calls a memory callback. An empty list takes the 8 bytes
// of its terminator and the next list starts right after it, so 96 empty lists fit in 1 KiB of VDM
// and the 97th calls the callback. The setters and a reserve without a draw take nothing. The first
// draw of a list takes 0x28 VDM, 0x154 vertex and 0x5C fragment bytes, and each further draw 0x20
// VDM and 0x70 vertex bytes once the vertex head is 16 byte aligned, so 23 draws fit in 1 KiB of
// VDM. A state change and a draw take 0x28 VDM and 0xB0 vertex bytes, a viewport or vertex stream
// change and a draw 0xC0 vertex bytes, and setting the same programs again adds nothing. A draw
// without the reserve fails with 0x805B0011. A precomputed draw takes 0x20 VDM bytes like a plain
// one. Word 3 of the list is the last 32 pixel tile row of the region clip. The callback is asked
// for 0x400 bytes.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "gxm_deferred_callback_recorder_programs.h"
#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE
#define RING_SIZE (64 * KIB)
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define REGION_SIZE (64 * KIB)
#define MAX_REFILL_CALLS 1000

// The block's layout: the three rings, the 1 KiB VDM pieces of the callback, the callback's vertex
// and fragment memory, the vertex data and the precomputed draw memory
#define VDM_RING 0
#define VERTEX_RING (64 * KIB)
#define FRAGMENT_RING (128 * KIB)
#define VDM_PIECES (256 * KIB)
#define CALLBACK_VERTEX MIB
#define CALLBACK_FRAGMENT (2 * MIB)
#define VERTEX_DATA (3 * MIB)
#define PRECOMPUTED_MEM (3 * MIB + 4 * KIB)

int sceGxmGetDeferredContextVdmBuffer(SceGxmContext *context, void **mem);
int sceGxmGetDeferredContextVertexBuffer(SceGxmContext *context, void **mem);
int sceGxmGetDeferredContextFragmentBuffer(SceGxmContext *context, void **mem);
int sceGxmSetDeferredContextVdmBuffer(SceGxmContext *context, void *mem, unsigned int size);
int sceGxmSetDeferredContextVertexBuffer(SceGxmContext *context, void *mem, unsigned int size);
int sceGxmSetDeferredContextFragmentBuffer(SceGxmContext *context, void *mem, unsigned int size);

enum { RING_VDM, RING_VERTEX, RING_FRAGMENT, RING_COUNT };

static SceUID block;
static char *base;
static SceGxmContext *context;
static char host_mem[HOST_SIZE + 64] __attribute__((aligned(16)));
static SceGxmVertexProgram *vertex_program;
static SceGxmFragmentProgram *fragment_program;
static SceGxmPrecomputedDraw precomputed;
static const unsigned short *indices;

static volatile int callbacks[RING_COUNT];
static volatile unsigned int last_request[RING_COUNT];
static unsigned int vdm_pieces;
static int first_error;

static char host_pool[REGION_SIZE] __attribute__((aligned(16)));
static unsigned int host_used;

static void *vdm_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    callbacks[RING_VDM]++;
    last_request[RING_VDM] = requested;
    *size = KIB;
    return base + VDM_PIECES + (vdm_pieces++ % 256) * KIB;
}

static void *vertex_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    callbacks[RING_VERTEX]++;
    last_request[RING_VERTEX] = requested;
    *size = RING_SIZE;
    return base + CALLBACK_VERTEX;
}

static void *fragment_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    callbacks[RING_FRAGMENT]++;
    last_request[RING_FRAGMENT] = requested;
    *size = RING_SIZE;
    return base + CALLBACK_FRAGMENT;
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

static void set_draw_state(void) {
    sceGxmSetVertexProgram(context, vertex_program);
    sceGxmSetFragmentProgram(context, fragment_program);
    note(sceGxmSetVertexStream(context, 0, base + VERTEX_DATA));
}

// The same value of every state a case changes, set at the start of each list, so a case's first
// change always changes the state and no case sees what the one before it left
static void set_baseline(void) {
    sceGxmSetCullMode(context, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(context, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetFrontDepthBias(context, 0, 0);
    sceGxmSetFrontStencilRef(context, 0);
    sceGxmSetFrontStencilFunc(context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
    sceGxmSetFrontPolygonMode(context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetFrontPointLineWidth(context, 1);
    sceGxmSetTwoSidedEnable(context, SCE_GXM_TWO_SIDED_DISABLED);
    sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, 31, 31);
    sceGxmSetViewport(context, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
}

static void reserve(void) {
    void *uniforms;
    note(sceGxmReserveVertexDefaultUniformBuffer(context, &uniforms));
}

static void draw(void) {
    note(sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, indices, 3));
}

static void reserve_and_draw(void) {
    reserve();
    draw();
}

static void case_empty(int i) {
    (void)i;
}

static void case_setters(int i) {
    sceGxmSetCullMode(context, i & 1 ? SCE_GXM_CULL_CW : SCE_GXM_CULL_CCW);
    sceGxmSetFrontDepthFunc(context, i & 1 ? SCE_GXM_DEPTH_FUNC_LESS : SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontStencilRef(context, i + 1);
    sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, 64 + i, 64);
    sceGxmSetViewport(context, 1.0f + i, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
    set_draw_state();
}

static void case_draws(int i) {
    if (i == 0)
        set_draw_state();
    reserve_and_draw();
}

static void case_draws_no_reserve(int i) {
    if (i == 0)
        set_draw_state();
    draw();
}

static void case_two_reserves(int i) {
    if (i == 0)
        set_draw_state();
    reserve();
    reserve_and_draw();
}

static void case_reserve_only(int i) {
    if (i == 0)
        set_draw_state();
    reserve();
}

// A draw, then per repeat one state change and a draw
static void changed_state_case(int i, void (*change)(int)) {
    if (i == 0) {
        set_draw_state();
        reserve_and_draw();
    }
    change(i);
    reserve_and_draw();
}

static void change_cull(int i) {
    sceGxmSetCullMode(context, i & 1 ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_CW);
}

static void change_depth_func(int i) {
    sceGxmSetFrontDepthFunc(context, i & 1 ? SCE_GXM_DEPTH_FUNC_ALWAYS : SCE_GXM_DEPTH_FUNC_LESS);
}

static void change_depth_write(int i) {
    sceGxmSetFrontDepthWriteEnable(context, i & 1 ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
}

static void change_depth_bias(int i) {
    sceGxmSetFrontDepthBias(context, i + 1, i + 1);
}

static void change_stencil_ref(int i) {
    sceGxmSetFrontStencilRef(context, i + 1);
}

static void change_stencil_func(int i) {
    sceGxmSetFrontStencilFunc(context, i & 1 ? SCE_GXM_STENCIL_FUNC_ALWAYS : SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
}

static void change_polygon_mode(int i) {
    sceGxmSetFrontPolygonMode(context, i & 1 ? SCE_GXM_POLYGON_MODE_TRIANGLE_FILL : SCE_GXM_POLYGON_MODE_TRIANGLE_LINE);
}

static void change_point_line_width(int i) {
    sceGxmSetFrontPointLineWidth(context, i + 2);
}

static void change_two_sided(int i) {
    sceGxmSetTwoSidedEnable(context, i & 1 ? SCE_GXM_TWO_SIDED_DISABLED : SCE_GXM_TWO_SIDED_ENABLED);
}

// Two 32 pixel tiles apart from the baseline and from each other
static void change_region_clip(int i) {
    sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, 64 * (i + 1) + 31, 64 * (i + 1) + 31);
}

static void change_viewport(int i) {
    sceGxmSetViewport(context, 2.0f + i, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
}

static void change_vertex_stream(int i) {
    note(sceGxmSetVertexStream(context, 0, base + VERTEX_DATA + 64 * (i + 1)));
}

static void change_same_programs(int i) {
    (void)i;
    sceGxmSetVertexProgram(context, vertex_program);
    sceGxmSetFragmentProgram(context, fragment_program);
}

static void case_cull(int i) {
    changed_state_case(i, change_cull);
}

static void case_depth_func(int i) {
    changed_state_case(i, change_depth_func);
}

static void case_depth_write(int i) {
    changed_state_case(i, change_depth_write);
}

static void case_depth_bias(int i) {
    changed_state_case(i, change_depth_bias);
}

static void case_stencil_ref(int i) {
    changed_state_case(i, change_stencil_ref);
}

static void case_stencil_func(int i) {
    changed_state_case(i, change_stencil_func);
}

static void case_polygon_mode(int i) {
    changed_state_case(i, change_polygon_mode);
}

static void case_point_line_width(int i) {
    changed_state_case(i, change_point_line_width);
}

static void case_two_sided(int i) {
    changed_state_case(i, change_two_sided);
}

static void case_region_clip(int i) {
    changed_state_case(i, change_region_clip);
}

static void case_viewport(int i) {
    changed_state_case(i, change_viewport);
}

static void case_vertex_stream(int i) {
    changed_state_case(i, change_vertex_stream);
}

static void case_same_programs(int i) {
    changed_state_case(i, change_same_programs);
}

static void case_precomputed(int i) {
    if (i == 0)
        set_draw_state();
    reserve();
    note(sceGxmDrawPrecomputed(context, &precomputed));
}

struct ring_case {
    const char *name;
    void (*calls)(int i);
};

static const struct ring_case cases[] = {
    { "an empty list", case_empty },
    { "state setters", case_setters },
    { "reserves and draws", case_draws },
    { "draws without a reserve", case_draws_no_reserve },
    { "two reserves before each draw", case_two_reserves },
    { "reserves without a draw", case_reserve_only },
    { "draws with a cull mode change", case_cull },
    { "draws with a depth function change", case_depth_func },
    { "draws with a depth write change", case_depth_write },
    { "draws with a depth bias change", case_depth_bias },
    { "draws with a stencil reference change", case_stencil_ref },
    { "draws with a stencil function change", case_stencil_func },
    { "draws with a polygon mode change", case_polygon_mode },
    { "draws with a point and line width change", case_point_line_width },
    { "draws with a two sided mode change", case_two_sided },
    { "draws with a region clip change", case_region_clip },
    { "draws with a viewport change", case_viewport },
    { "draws with a vertex stream change", case_vertex_stream },
    { "draws with the same programs set again", case_same_programs },
    { "precomputed draws", case_precomputed },
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
        if (repeats == 1)
            PRINTF(list.words[2], "%s: command list word 2", c->name);
        if (repeats == 1)
            PRINTF(list.words[3], "%s: command list word 3", c->name);
    }
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));

    block = sceKernelAllocMemBlock("ring usage", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * MIB, NULL);
    PRINT("allocate the block", test_created(block));
    sceKernelGetMemBlockBase(block, (void **)&base);
    PRINT("map the block", sceGxmMapMemory(base, 4 * MIB, RW));

    SceGxmDeferredContextParams params = { 0 };
    params.hostMem = host_mem;
    params.hostMemSize = HOST_SIZE;
    params.vdmCallback = vdm_callback;
    params.vertexCallback = vertex_callback;
    params.fragmentCallback = fragment_callback;
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

    SceGxmShaderPatcherId vertex_id = NULL, fragment_id = NULL;
    PRINT("register the vertex program", sceGxmShaderPatcherRegisterProgram(patcher, (const SceGxmProgram *)color_v_gxp, &vertex_id));
    PRINT("register the fragment program", sceGxmShaderPatcherRegisterProgram(patcher, (const SceGxmProgram *)color_f_gxp, &fragment_id));
    const SceGxmProgram *vertex_gxp = sceGxmShaderPatcherGetProgramFromId(vertex_id);

    // A position of 3 floats and a color of 4 bytes, 16 bytes per vertex
    SceGxmVertexAttribute attributes[2] = {
        { .streamIndex = 0, .offset = 0, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32, .componentCount = 3 },
        { .streamIndex = 0, .offset = 12, .format = SCE_GXM_ATTRIBUTE_FORMAT_U8N, .componentCount = 4 },
    };
    attributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(vertex_gxp, "aPosition"));
    attributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(vertex_gxp, "aColor"));
    const SceGxmVertexStream stream = { .stride = 16, .indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
    PRINT("create the vertex program", sceGxmShaderPatcherCreateVertexProgram(patcher, vertex_id, attributes, 2, &stream, 1, &vertex_program));
    PRINT("create the fragment program", sceGxmShaderPatcherCreateFragmentProgram(patcher, fragment_id,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL, vertex_gxp, &fragment_program));

    // One triangle, and the three indices after it
    float *vertices = (float *)(base + VERTEX_DATA);
    for (int i = 0; i < 12; i++)
        vertices[i] = (float)(i % 3);
    unsigned short *index_data = (unsigned short *)(base + VERTEX_DATA + 256);
    for (int i = 0; i < 3; i++)
        index_data[i] = i;
    indices = index_data;

    PRINT("the precomputed draw fits its memory", sceGxmGetPrecomputedDrawSize(vertex_program) <= 4 * KIB);
    PRINT("init the precomputed draw", sceGxmPrecomputedDrawInit(&precomputed, vertex_program, base + PRECOMPUTED_MEM));
    const void *streams[1] = { base + VERTEX_DATA };
    PRINT("set the precomputed draw's stream", sceGxmPrecomputedDrawSetAllVertexStreams(&precomputed, streams));
    sceGxmPrecomputedDrawSetParams(&precomputed, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, indices, 3);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);
    PRINT("memory callbacks during the cases", callbacks[RING_VDM] + callbacks[RING_VERTEX] + callbacks[RING_FRAGMENT]);

    // Two lists one after the other on the same VDM buffer
    first_error = 0;
    set_rings();
    SceGxmCommandList first = { 0 }, second = { 0 };
    note(sceGxmBeginCommandList(context));
    note(sceGxmEndCommandList(context, &first));
    note(sceGxmBeginCommandList(context));
    note(sceGxmEndCommandList(context, &second));
    PRINT("two lists: first error", first_error);
    const int in_buffer = vdm_bytes(&first, base + VDM_RING, RING_SIZE) >= 0 && vdm_bytes(&second, base + VDM_RING, RING_SIZE) >= 0;
    PRINT("two lists: the second starts this many bytes after the first's terminator", in_buffer ? (int)(second.words[0] - first.words[1]) : -1);
    PRINT("two lists: the context ID word is the same", first.words[7] == second.words[7]);
    PRINT("two lists: words 4 to 6 are 0", (first.words[4] | first.words[5] | first.words[6]) == 0);

    // A 1 KiB VDM buffer: how many empty lists fit, then how many draws, before the VDM callback runs
    first_error = 0;
    set_rings();
    note(sceGxmSetDeferredContextVdmBuffer(context, base + VDM_PIECES + 255 * KIB, KIB));
    const int before = callbacks[RING_VDM];
    int lists = 0;
    while (callbacks[RING_VDM] == before && lists < MAX_REFILL_CALLS) {
        SceGxmCommandList list = { 0 };
        note(sceGxmBeginCommandList(context));
        note(sceGxmEndCommandList(context, &list));
        lists++;
    }
    PRINT("1 KiB VDM: first error of the empty lists", first_error);
    PRINT("1 KiB VDM: empty lists until the callback ran, the last one included", lists);
    PRINT("1 KiB VDM: the callback's request for an empty list", last_request[RING_VDM]);

    first_error = 0;
    set_rings();
    note(sceGxmSetDeferredContextVdmBuffer(context, base + VDM_PIECES + 255 * KIB, KIB));
    const int before_draws = callbacks[RING_VDM];
    note(sceGxmBeginCommandList(context));
    set_baseline();
    set_draw_state();
    int draws = 0;
    while (callbacks[RING_VDM] == before_draws && draws < MAX_REFILL_CALLS) {
        reserve_and_draw();
        draws++;
    }
    SceGxmCommandList list = { 0 };
    note(sceGxmEndCommandList(context, &list));
    PRINT("1 KiB VDM: first error of the draws", first_error);
    PRINT("1 KiB VDM: draws until the callback ran, the last one included", draws);
    PRINT("1 KiB VDM: the callback's request for a draw", last_request[RING_VDM]);
    PRINT("vertex and fragment callbacks", callbacks[RING_VERTEX] + callbacks[RING_FRAGMENT]);

    PRINT("destroy the deferred context", sceGxmDestroyDeferredContext(context));
    PRINT("release the fragment program", sceGxmShaderPatcherReleaseFragmentProgram(patcher, fragment_program));
    PRINT("release the vertex program", sceGxmShaderPatcherReleaseVertexProgram(patcher, vertex_program));
    PRINT("unregister the fragment program", sceGxmShaderPatcherUnregisterProgram(patcher, fragment_id));
    PRINT("unregister the vertex program", sceGxmShaderPatcherUnregisterProgram(patcher, vertex_id));
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
