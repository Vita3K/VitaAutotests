// sceKernelOpenMemBlock on the name of an object that is not a memory block. A semaphore and an
// event flag are made under the names "other class sema" and "other class evf", and the test opens
// each name as a memory block with flags 0x10, 0x30 and 0, next to a name nobody has. The results
// are printed, and whether the semaphore and the event flag still open by their own calls
// afterwards.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static void try_open(const char *label, const char *name) {
    static const SceUInt32 flags[3] = { 0x10, 0x30, 0 };
    for (int i = 0; i < 3; i++) {
        const SceUID block = sceKernelOpenMemBlock(name, flags[i]);
        PRINTF(test_created(block), "%s, flags 0x%X: open as a memory block", label, (unsigned)flags[i]);
        if (block >= 0)
            sceKernelCloseMemBlock(block);
    }
}

int main(void) {
    const SceUID sema = sceKernelCreateSema("other class sema", SCE_KERNEL_ATTR_OPENABLE, 0, 1, NULL);
    const SceUID evf = sceKernelCreateEventFlag("other class evf", SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    PRINT("create the semaphore", test_created(sema));
    PRINT("create the event flag", test_created(evf));
    try_open("the semaphore's name", "other class sema");
    try_open("the event flag's name", "other class evf");
    try_open("a name nobody has", "other class none");
    const SceUID sema_open = sceKernelOpenSema("other class sema");
    const SceUID evf_open = sceKernelOpenEventFlag("other class evf");
    PRINT("the semaphore opens by its own call", test_created(sema_open));
    PRINT("the event flag opens by its own call", test_created(evf_open));
    if (sema_open >= 0)
        sceKernelCloseSema(sema_open);
    if (evf_open >= 0)
        sceKernelCloseEventFlag(evf_open);
    sceKernelDeleteSema(sema);
    sceKernelDeleteEventFlag(evf);
    test_exit();
    return 0;
}
