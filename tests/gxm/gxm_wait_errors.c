// The errors of sceGxmFinish and sceGxmNotificationWait, outside and inside the display queue
// callback. No GPU work runs, so no call here ever has to wait. The cases:
// - sceGxmNotificationWait with a NULL notification, with an address outside the notification
//   region (a static word, with its own value as the one to wait for) and with a word of the region
//   that already has the value
// - sceGxmFinish with NULL, with an immediate context and with a deferred context
// - one display queue entry. Its callback runs the same calls on the display queue's thread, plus
//   sceGxmFinish on the deferred context. The test polls until the callback is done, so a callback
//   that never runs costs 5 seconds and the output shows it. Then sceGxmDisplayQueueFinish.
// The callback stores the results and the main thread prints them, so the callback does no I/O.
// sceGxmFinish returns void in the headers, but it returns an error code.
//
// On a PS Vita TV on firmware 3.65 a NULL notification fails with 0x805B0004 and an address outside
// the region with 0x805B0003, and a word of the region that already has its value gives 0.
// sceGxmFinish gives 0x805B0004 for NULL, 0 for the immediate context and 0x805B0003 for the
// deferred context. In the display queue callback every sceGxmNotificationWait gives 0x805B0018,
// also the one with NULL. There sceGxmFinish gives 0x805B0004 for NULL and 0x805B0003 for the
// deferred context, and only the immediate context gives 0x805B0018. So the checks for NULL and for
// the deferred context come before the one for the display queue's thread. sceGxmDisplayQueueFinish
// after the callback ran gives 0.
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define KIB 0x400
#define MIB 0x100000

#define HOST_SIZE SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE

// sceGxmFinish returns void in the headers, this is the same function with its result
int gxm_finish(SceGxmContext *context) __asm__("sceGxmFinish");

static char host_mem[2][HOST_SIZE + 64] __attribute__((aligned(16)));

// The blocks of one context
typedef struct Setup {
    SceUID rings, usse;
    char *rings_base, *usse_base;
    unsigned int usse_offset;
} Setup;

// What the display queue callback does and finds
static SceGxmContext *immediate, *deferred;
static SceGxmNotification outside_notification, region_notification;
static volatile unsigned int outside_word;
static volatile int callback_done;
static int callback_results[6];

static void display_callback(const void *data) {
    (void)data;
    callback_results[0] = sceGxmNotificationWait(NULL);
    callback_results[1] = sceGxmNotificationWait(&outside_notification);
    callback_results[2] = sceGxmNotificationWait(&region_notification);
    callback_results[3] = gxm_finish(NULL);
    callback_results[4] = gxm_finish(immediate);
    callback_results[5] = gxm_finish(deferred);
    callback_done = 1;
}

static int initialize(void) {
    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 2;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = 4;
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    return sceGxmInitialize(&init);
}

static char *alloc(SceUID *uid, SceSize size) {
    *uid = sceKernelAllocMemBlock("gxm wait errors", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
                                  NULL);
    void *base = NULL;
    if (*uid >= 0)
        sceKernelGetMemBlockBase(*uid, &base);
    return base;
}

// The ring buffers are 4 MiB mapped with READ, the VDM ring buffer first, then the fragment ring
// buffer at 256 KiB and the vertex ring buffer at 1 MiB. The fragment USSE ring buffer is a block of
// its own, only the immediate context has one.
static void setup(Setup *s, int usse) {
    s->rings_base = alloc(&s->rings, 4 * MIB);
    sceGxmMapMemory(s->rings_base, 4 * MIB, SCE_GXM_MEMORY_ATTRIB_READ);
    s->usse = -1;
    if (usse) {
        s->usse_base = alloc(&s->usse, 64 * KIB);
        sceGxmMapFragmentUsseMemory(s->usse_base, 64 * KIB, &s->usse_offset);
    }
}

static void teardown(Setup *s) {
    if (s->usse >= 0) {
        sceGxmUnmapFragmentUsseMemory(s->usse_base);
        sceKernelFreeMemBlock(s->usse);
    }
    sceGxmUnmapMemory(s->rings_base);
    sceKernelFreeMemBlock(s->rings);
}

static void *no_memory(void *args, SceSize requested, SceSize *size) {
    (void)args;
    (void)requested;
    *size = 0;
    return NULL;
}

static int create_immediate(Setup *s, SceGxmContext **context) {
    SceGxmContextParams params = { 0 };
    params.hostMem = host_mem[0];
    params.hostMemSize = HOST_SIZE;
    params.vdmRingBufferMem = s->rings_base;
    params.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    params.vertexRingBufferMem = s->rings_base + MIB;
    params.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    params.fragmentRingBufferMem = s->rings_base + 256 * KIB;
    params.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferMem = s->usse_base;
    params.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    params.fragmentUsseRingBufferOffset = s->usse_offset;
    return sceGxmCreateContext(&params, context);
}

static int create_deferred(Setup *s, SceGxmContext **context) {
    SceGxmDeferredContextParams params = { 0 };
    params.hostMem = host_mem[1];
    params.hostMemSize = HOST_SIZE;
    params.vdmCallback = no_memory;
    params.vertexCallback = no_memory;
    params.fragmentCallback = no_memory;
    params.vdmRingBufferMem = s->rings_base;
    params.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    params.vertexRingBufferMem = s->rings_base + MIB;
    params.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    params.fragmentRingBufferMem = s->rings_base + 256 * KIB;
    params.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    return sceGxmCreateDeferredContext(&params, context);
}

int main(void) {
    PRINT("initialize libgxm", initialize());

    outside_notification = (SceGxmNotification){ &outside_word, outside_word };
    volatile unsigned int *const region = sceGxmGetNotificationRegion();
    region_notification = (SceGxmNotification){ region, *region };
    PRINT("notification wait: NULL", sceGxmNotificationWait(NULL));
    PRINT("notification wait: outside the region", sceGxmNotificationWait(&outside_notification));
    PRINT("notification wait: in the region, already equal",
          sceGxmNotificationWait(&region_notification));
    PRINT("finish: NULL", gxm_finish(NULL));

    Setup first, second;
    setup(&first, 1);
    setup(&second, 0);
    const int created = create_immediate(&first, &immediate);
    PRINT("finish: create the immediate context", created);
    if (created == 0)
        PRINT("finish: the immediate context", gxm_finish(immediate));
    const int deferred_created = create_deferred(&second, &deferred);
    PRINT("finish: create the deferred context", deferred_created);
    if (deferred_created == 0)
        PRINT("finish: the deferred context", gxm_finish(deferred));

    // One entry, so its callback runs on the display queue's thread
    SceGxmSyncObject *old_buffer = NULL, *new_buffer = NULL;
    PRINT("create a sync object", sceGxmSyncObjectCreate(&old_buffer));
    PRINT("create another sync object", sceGxmSyncObjectCreate(&new_buffer));
    int data = 0;
    const int added = sceGxmDisplayQueueAddEntry(old_buffer, new_buffer, &data);
    PRINT("display queue: add an entry", added);
    if (added == 0) {
        TEST_WAIT_FOR(callback_done);
        PRINT("display queue: the callback ran", callback_done);
        if (callback_done) {
            PRINT("display queue: finish it", sceGxmDisplayQueueFinish());
            PRINT("callback: notification wait with NULL", callback_results[0]);
            PRINT("callback: notification wait outside the region", callback_results[1]);
            PRINT("callback: notification wait in the region", callback_results[2]);
            PRINT("callback: finish with NULL", callback_results[3]);
            PRINT("callback: finish the immediate context", callback_results[4]);
            PRINT("callback: finish the deferred context", callback_results[5]);
        }
    }

    if (deferred_created == 0)
        PRINT("destroy the deferred context", sceGxmDestroyDeferredContext(deferred));
    if (created == 0)
        PRINT("destroy the immediate context", sceGxmDestroyContext(immediate));
    sceGxmSyncObjectDestroy(new_buffer);
    sceGxmSyncObjectDestroy(old_buffer);
    teardown(&second);
    teardown(&first);
    PRINT("terminate libgxm", sceGxmTerminate());
    test_exit();
    return 0;
}
