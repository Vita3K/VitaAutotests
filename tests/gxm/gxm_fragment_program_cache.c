// Which arguments of sceGxmShaderPatcherCreateFragmentProgram make the shader patcher hand out a
// new fragment program, and which give back the one it made before. The programs are the compiled
// shaders of libvita2d in a header next to another test. The fragment program is clear_f, which
// reads no vertex output, and the vertex programs passed for the output mapping are clear_v,
// which has only a position output, texture_v, which also has a texture coordinate, and NULL. The
// patcher has static regions and host memory from malloc. The base call has the UCHAR4 output
// format, no multisampling, no blend info and clear_v. The blend is an alpha blend, color ADD of
// SRC_ALPHA and ONE_MINUS_SRC_ALPHA, alpha ADD of ONE and ZERO, all four color channels. Each case
// makes two programs that differ as it says and prints both results and whether they are the same
// object.
// The cases:
// - the base call twice, and the alpha blend twice
// - clear_f and texture_f with texture_v
// - the UCHAR4 and HALF4 output formats, with no blend info and with the alpha blend
// - clear_v and texture_v, and clear_v and NULL
// - no multisampling and 4X, with no blend info and with the alpha blend
// - no blend info and a blend that doesn't blend, color and alpha NONE with all four channels
// - no blend info and the alpha blend
// Then every program is released, and the results of the releases, the unregisters and the
// patcher's destroy are printed.
//
// On a PS Vita TV on firmware 3.65 the same arguments give the same program back, and so do no
// multisampling and 4X without blend info, and no blend info and a blend that doesn't blend.
// Another fragment program, output format, vertex program (NULL included) or blend, and 4X with
// the alpha blend, each give a new program. Every create, release and unregister works.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>

#include "gxm_deferred_ring_usage_more_programs.h"
#include "test.h"

#define KIB 0x400

#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define REGION_SIZE (64 * KIB)
#define MAX_PROGRAMS 32

static SceGxmShaderPatcher *patcher;
static SceGxmShaderPatcherId clear_id, texture_id;
static SceGxmFragmentProgram *programs[MAX_PROGRAMS];
static unsigned int program_count;

static const SceGxmBlendInfo alpha_blend = {
    .colorMask = SCE_GXM_COLOR_MASK_ALL,
    .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
    .alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
    .colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
    .colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .alphaSrc = SCE_GXM_BLEND_FACTOR_ONE,
    .alphaDst = SCE_GXM_BLEND_FACTOR_ZERO,
};

static const SceGxmBlendInfo no_blend = {
    .colorMask = SCE_GXM_COLOR_MASK_ALL,
    .colorFunc = SCE_GXM_BLEND_FUNC_NONE,
    .alphaFunc = SCE_GXM_BLEND_FUNC_NONE,
    .colorSrc = SCE_GXM_BLEND_FACTOR_ONE,
    .colorDst = SCE_GXM_BLEND_FACTOR_ZERO,
    .alphaSrc = SCE_GXM_BLEND_FACTOR_ONE,
    .alphaDst = SCE_GXM_BLEND_FACTOR_ZERO,
};

// The arguments of one create
typedef struct Args {
    SceGxmShaderPatcherId id;
    SceGxmOutputRegisterFormat format;
    SceGxmMultisampleMode multisample;
    const SceGxmBlendInfo *blend;
    const void *vertex;
} Args;

static void display_callback(const void *data) {
    (void)data;
}

static void *host_alloc(void *user_data, SceSize size) {
    (void)user_data;
    return malloc(size);
}

static void host_free(void *user_data, void *mem) {
    (void)user_data;
    free(mem);
}

static Args base_args(void) {
    Args args = { clear_id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL,
                  clear_v_gxp };
    return args;
}

// Makes one program and prints the result, and keeps it for the release at the end
static SceGxmFragmentProgram *create(const char *what, const char *which, const Args *args) {
    SceGxmFragmentProgram *program = NULL;
    const int got = sceGxmShaderPatcherCreateFragmentProgram(
        patcher, args->id, args->format, args->multisample, args->blend,
        (const SceGxmProgram *)args->vertex, &program);
    PRINTF(got, "%s: create the %s", what, which);
    if (got == 0 && program_count < MAX_PROGRAMS)
        programs[program_count++] = program;
    return got == 0 ? program : NULL;
}

static void pair(const char *what, const Args *first, const Args *second) {
    SceGxmFragmentProgram *const a = create(what, "first", first);
    SceGxmFragmentProgram *const b = create(what, "second", second);
    if (a && b)
        PRINTF(a == b, "%s: the same object", what);
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));

    // The patcher's buffer, vertex USSE and fragment USSE regions, each in a block of its own
    SceUID region_block[3];
    char *region_base[3];
    for (int i = 0; i < 3; i++) {
        region_block[i] = sceKernelAllocMemBlock("fragment cache",
                                                 SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                                 REGION_SIZE, NULL);
        sceKernelGetMemBlockBase(region_block[i], (void **)&region_base[i]);
    }
    unsigned int vertex_usse_offset = 0, fragment_usse_offset = 0;
    sceGxmMapMemory(region_base[0], REGION_SIZE, RW);
    sceGxmMapVertexUsseMemory(region_base[1], REGION_SIZE, &vertex_usse_offset);
    sceGxmMapFragmentUsseMemory(region_base[2], REGION_SIZE, &fragment_usse_offset);

    SceGxmShaderPatcherParams params = { 0 };
    params.hostAllocCallback = host_alloc;
    params.hostFreeCallback = host_free;
    params.bufferMem = region_base[0];
    params.bufferMemSize = REGION_SIZE;
    params.vertexUsseMem = region_base[1];
    params.vertexUsseMemSize = REGION_SIZE;
    params.vertexUsseOffset = vertex_usse_offset;
    params.fragmentUsseMem = region_base[2];
    params.fragmentUsseMemSize = REGION_SIZE;
    params.fragmentUsseOffset = fragment_usse_offset;
    PRINT("create the shader patcher", sceGxmShaderPatcherCreate(&params, &patcher));
    PRINT("register clear_f", sceGxmShaderPatcherRegisterProgram(
                                  patcher, (const SceGxmProgram *)clear_f_gxp, &clear_id));
    PRINT("register texture_f", sceGxmShaderPatcherRegisterProgram(
                                    patcher, (const SceGxmProgram *)texture_f_gxp, &texture_id));

    const Args base = base_args();
    Args other = base;
    pair("the base call twice", &base, &other);

    Args blended = base;
    blended.blend = &alpha_blend;
    other = blended;
    pair("the alpha blend twice", &blended, &other);

    Args clear = base, texture = base;
    clear.vertex = texture_v_gxp;
    texture.id = texture_id;
    texture.vertex = texture_v_gxp;
    pair("clear_f and texture_f", &clear, &texture);

    other = base;
    other.format = SCE_GXM_OUTPUT_REGISTER_FORMAT_HALF4;
    pair("UCHAR4 and HALF4", &base, &other);
    other = blended;
    other.format = SCE_GXM_OUTPUT_REGISTER_FORMAT_HALF4;
    pair("UCHAR4 and HALF4 with the alpha blend", &blended, &other);

    other = base;
    other.vertex = texture_v_gxp;
    pair("clear_v and texture_v", &base, &other);
    other.vertex = NULL;
    pair("clear_v and NULL", &base, &other);

    other = base;
    other.multisample = SCE_GXM_MULTISAMPLE_4X;
    pair("no multisampling and 4X", &base, &other);
    other = blended;
    other.multisample = SCE_GXM_MULTISAMPLE_4X;
    pair("no multisampling and 4X with the alpha blend", &blended, &other);

    other = base;
    other.blend = &no_blend;
    pair("no blend info and a blend that doesn't blend", &base, &other);
    pair("no blend info and the alpha blend", &base, &blended);

    int released = 0;
    for (unsigned int i = 0; i < program_count; i++) {
        const int got = sceGxmShaderPatcherReleaseFragmentProgram(patcher, programs[i]);
        if (got != 0 && released == 0)
            released = got;
    }
    PRINT("the first failing release", released);
    PRINT("unregister clear_f", sceGxmShaderPatcherUnregisterProgram(patcher, clear_id));
    PRINT("unregister texture_f", sceGxmShaderPatcherUnregisterProgram(patcher, texture_id));
    PRINT("destroy the shader patcher", sceGxmShaderPatcherDestroy(patcher));

    sceGxmUnmapMemory(region_base[0]);
    sceGxmUnmapVertexUsseMemory(region_base[1]);
    sceGxmUnmapFragmentUsseMemory(region_base[2]);
    for (int i = 0; i < 3; i++)
        sceKernelFreeMemBlock(region_block[i]);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
