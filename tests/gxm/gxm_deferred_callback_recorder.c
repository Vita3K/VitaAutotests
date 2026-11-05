// Which thread libgxm calls a deferred context's memory callbacks on when the thread that records
// into the command list is not the one that called sceGxmBeginCommandList. The context gets no ring
// memory, and its callbacks hand out a VDM ring of 1 KiB, a new piece of a mapped block each time.
// Thread A, on core 0, calls the begin, whose VDM callback records which thread it ran on. Thread B,
// on core 1, then sets a vertex and a fragment program and a vertex stream on the same context and
// draws a triangle over and over, reserving the default uniform buffer of the vertex program before
// each draw. It stops when a VDM callback ran on B or after 1000 draws, while A waits for B and then
// ends the command list. The programs are compiled GXM programs in a header next to the test, and a
// shader patcher with static regions makes the vertex and fragment programs from them. The callbacks
// only store the thread and the ring in variables and the main thread prints them, so no callback
// does I/O. The output has the results of the setup, the first failing call of B, whether the VDM
// callback of the begin ran on A, whether a callback ran on B during its calls, whether every one
// did, whether one ran on A, and which rings needed a refill.
//
// On a PS Vita TV on firmware 3.65 every call succeeds. The VDM callback of the begin runs on A.
// During B's draws the VDM, vertex and fragment rings all need a refill, and every one of those
// callbacks runs on B, none on A.
#include <psp2/gxm.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>

#include "gxm_deferred_callback_recorder_programs.h"
#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE
#define MAX_CALLS 1000
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define REGION_SIZE (64 * KIB)
#define VERTEX_DATA (3 * MIB)

enum { RING_VDM, RING_VERTEX, RING_FRAGMENT, RING_COUNT };

// What the callbacks see while B makes its calls
#define PHASE_BEGIN 0
#define PHASE_B_CALLS 1
#define PHASE_AFTER 2

static SceUID block;
static char *base;
static SceGxmContext *context;
static char host_mem[HOST_SIZE + 64] __attribute__((aligned(16)));
static SceUID begun_sema, done_sema;

static volatile SceUID a_thread_id, b_thread_id;
static volatile int phase = PHASE_BEGIN;
static volatile int begin_callback_on_a;
static volatile int on_b[RING_COUNT], on_a[RING_COUNT], on_other[RING_COUNT];
static volatile int a_begin = -1, a_end = -1;
static volatile int a_done, b_done, b_wait = -1;
static volatile unsigned int vdm_pieces;
static volatile int b_error;
static char host_pool[REGION_SIZE] __attribute__((aligned(16)));
static unsigned int host_used;
static SceGxmVertexProgram *vertex_program;
static SceGxmFragmentProgram *fragment_program;

static void record(int ring) {
    const SceUID id = sceKernelGetThreadId();
    if (phase == PHASE_BEGIN) {
        if (ring == RING_VDM && id == a_thread_id)
            begin_callback_on_a = 1;
    } else if (phase == PHASE_B_CALLS) {
        if (id == b_thread_id)
            on_b[ring] = 1;
        else if (id == a_thread_id)
            on_a[ring] = 1;
        else
            on_other[ring] = 1;
    }
}

static int any_on_b(void) {
    return on_b[RING_VDM] || on_b[RING_VERTEX] || on_b[RING_FRAGMENT];
}

static void *vdm_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    (void)requested;
    record(RING_VDM);
    // A new piece of the block each time, so a ring never overlaps the one handed out before
    *size = KIB;
    return base + (vdm_pieces++ % 256) * KIB;
}

static void *vertex_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    (void)requested;
    record(RING_VERTEX);
    *size = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    return base + MIB;
}

static void *fragment_callback(void *args, SceSize requested, SceSize *size) {
    (void)args;
    (void)requested;
    record(RING_FRAGMENT);
    *size = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    return base + 256 * KIB;
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

static void note_error(int result) {
    if (result < 0 && b_error == 0)
        b_error = result;
}

static int a_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    a_thread_id = sceKernelGetThreadId();
    a_begin = sceGxmBeginCommandList(context);
    sceKernelSignalSema(begun_sema, 1);
    // B always signals, after a bounded wait and at most MAX_CALLS draws, so the list never ends
    // while B still records
    sceKernelWaitSema(done_sema, 1, NULL);
    SceGxmCommandList list;
    a_end = sceGxmEndCommandList(context, &list);
    a_done = 1;
    return 0;
}

static int b_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    b_wait = sceKernelWaitSema(begun_sema, 1, &timeout);
    b_thread_id = sceKernelGetThreadId();
    // Without A's begin B records nothing
    if (b_wait != 0) {
        sceKernelSignalSema(done_sema, 1);
        b_done = 1;
        return 0;
    }
    phase = PHASE_B_CALLS;
    sceGxmSetVertexProgram(context, vertex_program);
    sceGxmSetFragmentProgram(context, fragment_program);
    note_error(sceGxmSetVertexStream(context, 0, base + VERTEX_DATA));
    for (int i = 0; i < MAX_CALLS && !on_b[RING_VDM]; i++) {
        void *uniforms;
        note_error(sceGxmReserveVertexDefaultUniformBuffer(context, &uniforms));
        note_error(sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, base + VERTEX_DATA + 256, 3));
    }
    phase = PHASE_AFTER;
    sceKernelSignalSema(done_sema, 1);
    b_done = 1;
    return 0;
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));

    block = sceKernelAllocMemBlock("deferred callback", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * MIB, NULL);
    PRINT("allocate the block", test_created(block));
    sceKernelGetMemBlockBase(block, (void **)&base);
    sceGxmMapMemory(base, 4 * MIB, SCE_GXM_MEMORY_ATTRIB_READ);

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
        region_block[i] = sceKernelAllocMemBlock("deferred callback", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, REGION_SIZE, NULL);
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
    unsigned short *indices = (unsigned short *)(base + VERTEX_DATA + 256);
    for (int i = 0; i < 3; i++)
        indices[i] = i;

    begun_sema = sceKernelCreateSema("begun", 0, 0, 1, NULL);
    done_sema = sceKernelCreateSema("done", 0, 0, 1, NULL);
    const SceUID b = sceKernelCreateThread("b", b_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID a = sceKernelCreateThread("a", a_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(b, 0, NULL);
    sceKernelStartThread(a, 0, NULL);
    TEST_WAIT_FOR(a_done && b_done);
    sceKernelWaitThreadEnd(a, NULL, NULL);
    sceKernelWaitThreadEnd(b, NULL, NULL);
    sceKernelDeleteThread(a);
    sceKernelDeleteThread(b);

    int any_other = 0, any_a = 0;
    for (int ring = 0; ring < RING_COUNT; ring++) {
        any_other |= on_other[ring];
        any_a |= on_a[ring];
    }
    PRINT("B's first failing call", b_error);
    PRINT("A's begin", a_begin);
    PRINT("B's wait for A's begin", b_wait);
    PRINT("the VDM callback of the begin ran on A", begin_callback_on_a);
    PRINT("a callback ran on B during its calls", any_on_b());
    PRINT("every callback during B's calls ran on B", !any_a && !any_other);
    PRINT("a callback ran on A during B's calls", any_a);
    PRINT("the VDM ring needed a refill during B's calls", on_b[RING_VDM] || on_a[RING_VDM] || on_other[RING_VDM]);
    PRINT("the vertex ring needed a refill during B's calls", on_b[RING_VERTEX] || on_a[RING_VERTEX] || on_other[RING_VERTEX]);
    PRINT("the fragment ring needed a refill during B's calls", on_b[RING_FRAGMENT] || on_a[RING_FRAGMENT] || on_other[RING_FRAGMENT]);
    PRINT("A's end", a_end);

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
    sceKernelDeleteSema(begun_sema);
    sceKernelDeleteSema(done_sema);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
