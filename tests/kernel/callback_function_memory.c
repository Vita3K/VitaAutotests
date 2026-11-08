// Where the function of a callback may be, Thumb bit set: in the app's code, in a constant in the
// app's read-only data, in a USER_RW memory block and on the thread's stack. Each callback is deleted
// if it was made, and none is ever notified, so its function never runs.
//
// On a PS Vita TV on firmware 3.65 the functions in the app's code and in its read-only data are
// taken, the ones in a USER_RW block and on the stack give 0x80020006.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static const unsigned int read_only_words[4] = { 0x47704770, 0x47704770, 0x47704770, 0x47704770 };

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Creates a callback with fn and attr, deletes it if it was made, and returns the create's result
static int try_create(const char *name, SceUInt attr, void *fn) {
    const SceUID cb = sceKernelCreateCallback(name, attr, (SceKernelCallbackFunction)fn, NULL);
    if (cb >= 0)
        sceKernelDeleteCallback(cb);
    return test_created(cb);
}

static void *thumb(const void *p) {
    return (void *)((SceUIntPtr)p | 1);
}

int main(void) {
    PRINT("a function in the app's code", try_create("code", 0, (void *)callback));
    PRINT("a function in the app's read-only data", try_create("rodata", 0, thumb(read_only_words)));

    const SceUID block = sceKernelAllocMemBlock("cb rw", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(block, &base);
    PRINT("allocate a USER_RW block", test_created(block));
    PRINT("a function in a USER_RW block", try_create("rw", 0, thumb(base)));
    sceKernelFreeMemBlock(block);

    unsigned int stack_words[4] = { 0x47704770, 0x47704770, 0x47704770, 0x47704770 };
    PRINT("a function on the stack", try_create("stack", 0, thumb(stack_words)));

    test_exit();
    return 0;
}
