// Whether two threads can be inside the memory callbacks of two deferred contexts at the same time.
// Two deferred contexts get no ring buffer memory, so the first sceGxmBeginCommandList on each one
// calls its VDM callback. Thread A calls the begin on context 1. Its VDM callback signals semaphore
// S1, then waits on semaphore S2 for 2 seconds. Thread B waits on S1, calls the begin on context 2
// and signals S2 once that call returned. B's VDM callback only records that it ran and whether A was
// still inside its own callback by then. If libgxm serialized the callbacks of different contexts,
// B's callback would not run before A's callback gave up on S2. A runs on core 0 and B on core 1, both
// at the main thread's priority, so neither waits for the other's core and only a lock in libgxm
// can hold one of them back. The callbacks store what they see and the main thread prints it, so no
// callback does I/O. The cases:
// - sceGxmInitialize, one 4 MiB block per context mapped with READ, whose memory the callbacks hand
//   out (VDM ring first, fragment ring at 256 KiB, vertex ring at 1 MiB), and the two contexts
// - the result of each begin, and B's wait for S1
// - whether B's VDM callback ran and whether it ran while A was inside its callback
// - A's wait for S2, 0 if B signaled it in time and the timeout error if B was held back
// - how often each VDM callback ran and whether the begin called a vertex or fragment callback
// - sceGxmEndCommandList on both contexts, then destroying them
//
// On a PS Vita TV on firmware 3.65 every call succeeds. B's VDM callback runs while A is still
// inside its own callback, and A's wait for S2 returns 0, so the callbacks of the two contexts aren't
// serialized. Each begin calls only its VDM callback, once.
#include <psp2/gxm.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE

// What one context's callbacks hand out and record. Index 0 is A's context.
typedef struct Context {
    SceUID block;
    char *base;
    SceGxmContext *context;
    volatile int vdm_runs, other_runs;
} Context;

static Context contexts[2];
static char host_mem[2][HOST_SIZE + 64] __attribute__((aligned(16)));
static SceUID s1, s2;

static volatile int in_a_callback, b_callback_ran, b_callback_during_a;
static volatile int a_wait_result = -1, b_wait_result = -1, a_begin = -1, b_begin = -1;
static volatile int a_done, b_done;

// Gives the callback's context the ring it asks for, at the default size of that ring
static void *give(void *args, int ring_offset, SceSize ring_size, SceSize *size) {
    Context *const c = args;
    *size = ring_size;
    return c->base + ring_offset;
}

static void *vdm_callback(void *args, SceSize requested, SceSize *size) {
    (void)requested;
    Context *const c = args;
    c->vdm_runs++;
    if (c == &contexts[0]) {
        in_a_callback = 1;
        sceKernelSignalSema(s1, 1);
        SceUInt timeout = TEST_WAIT_US;
        a_wait_result = sceKernelWaitSema(s2, 1, &timeout);
        in_a_callback = 0;
    } else {
        b_callback_during_a = in_a_callback;
        b_callback_ran = 1;
    }
    return give(args, 0, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, size);
}

static void *vertex_callback(void *args, SceSize requested, SceSize *size) {
    (void)requested;
    ((Context *)args)->other_runs++;
    return give(args, MIB, SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, size);
}

static void *fragment_callback(void *args, SceSize requested, SceSize *size) {
    (void)requested;
    ((Context *)args)->other_runs++;
    return give(args, 256 * KIB, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, size);
}

static void display_callback(const void *data) {
    (void)data;
}

static int a_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    a_begin = sceGxmBeginCommandList(contexts[0].context);
    a_done = 1;
    return 0;
}

static int b_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    b_wait_result = sceKernelWaitSema(s1, 1, &timeout);
    b_begin = sceGxmBeginCommandList(contexts[1].context);
    sceKernelSignalSema(s2, 1);
    b_done = 1;
    return 0;
}

static int create_context(int i) {
    Context *const c = &contexts[i];
    c->block = sceKernelAllocMemBlock("deferred callback", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * MIB, NULL);
    if (c->block < 0)
        return c->block;
    sceKernelGetMemBlockBase(c->block, (void **)&c->base);
    sceGxmMapMemory(c->base, 4 * MIB, SCE_GXM_MEMORY_ATTRIB_READ);

    SceGxmDeferredContextParams params = { 0 };
    params.hostMem = host_mem[i];
    params.hostMemSize = HOST_SIZE;
    params.vdmCallback = vdm_callback;
    params.vertexCallback = vertex_callback;
    params.fragmentCallback = fragment_callback;
    params.callbackData = c;
    return sceGxmCreateDeferredContext(&params, &c->context);
}

int main(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));
    PRINT("create deferred context 1", create_context(0));
    PRINT("create deferred context 2", create_context(1));
    s1 = sceKernelCreateSema("s1", 0, 0, 1, NULL);
    s2 = sceKernelCreateSema("s2", 0, 0, 1, NULL);

    // S1 keeps A's signal, so B gets it even if A signals before B waits
    const SceUID b = sceKernelCreateThread("b", b_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID a = sceKernelCreateThread("a", a_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(b, 0, NULL);
    sceKernelStartThread(a, 0, NULL);
    TEST_WAIT_FOR(a_done && b_done);

    const int a_runs = contexts[0].vdm_runs, b_runs = contexts[1].vdm_runs;
    const int other_runs = contexts[0].other_runs + contexts[1].other_runs;
    PRINT("A's begin", a_begin);
    PRINT("B's begin", b_begin);
    PRINT("B's wait for S1", b_wait_result);
    PRINT("B's callback ran", b_callback_ran);
    PRINT("B's callback ran while A was inside its callback", b_callback_during_a);
    PRINT("A's wait for S2", a_wait_result);
    PRINT("A's callback ran once", a_runs == 1);
    PRINT("B's callback ran once", b_runs == 1);
    PRINT("the begins called a vertex or fragment callback", other_runs != 0);

    sceKernelWaitThreadEnd(a, NULL, NULL);
    sceKernelWaitThreadEnd(b, NULL, NULL);
    sceKernelDeleteThread(a);
    sceKernelDeleteThread(b);

    SceGxmCommandList lists[2];
    for (int i = 0; i < 2; i++)
        PRINTF(sceGxmEndCommandList(contexts[i].context, &lists[i]), "end the command list of context %d", i + 1);
    for (int i = 0; i < 2; i++)
        PRINTF(sceGxmDestroyDeferredContext(contexts[i].context), "destroy deferred context %d", i + 1);
    for (int i = 0; i < 2; i++) {
        sceGxmUnmapMemory(contexts[i].base);
        sceKernelFreeMemBlock(contexts[i].block);
    }
    sceKernelDeleteSema(s1);
    sceKernelDeleteSema(s2);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
