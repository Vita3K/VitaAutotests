// Whether UIDs come back after a delete, and what opening gives
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    // A deleted UID handed out again
    const SceUID first = sceKernelCreateSema("first", 0, 0, 1, NULL);
    sceKernelDeleteSema(first);
    const SceUID second = sceKernelCreateSema("second", 0, 0, 1, NULL);
    PRINT("the next object gets the deleted UID back", second == first);
    PRINT("UIDs grow", second > first);
    PRINT("UIDs are odd", (second & 1) == 1);

    // Opening by name
    const SceUID sema = sceKernelCreateSema("open", ATTR_OPENABLE, 0, 2, NULL);
    const SceUID opened = sceKernelOpenSema("open");
    PRINT("an opened UID is the object's own", opened == sema);
    const SceUID again = sceKernelOpenSema("open");
    PRINT("opening twice gives the same UID", again == opened);
    PRINT("close the first open", sceKernelCloseSema(opened));
    PRINT("the second open still works", sceKernelSignalSema(again, 1));
    PRINT("close it", sceKernelCloseSema(again));
    PRINT("close it again", sceKernelCloseSema(again));
    PRINT("close the object's own UID", sceKernelCloseSema(sema));
    PRINT("the object after closing its own UID", sceKernelPollSema(sema, 1));

    // Deleting while opened
    const SceUID shared = sceKernelCreateSema("shared", ATTR_OPENABLE, 0, 2, NULL);
    const SceUID handle = sceKernelOpenSema("shared");
    PRINT("delete while opened", sceKernelDeleteSema(shared));
    PRINT("the opened UID after the delete", sceKernelSignalSema(handle, 1));
    PRINT("open a deleted object", sceKernelOpenSema("shared") < 0);
    PRINT("delete through an opened UID", sceKernelDeleteSema(handle));

    // Mutexes open the same way
    const SceUID mutex = sceKernelCreateMutex("mutex", ATTR_OPENABLE, 0, NULL);
    const SceUID opened_mutex = sceKernelOpenMutex("mutex");
    PRINT("an opened mutex UID is the mutex's own", opened_mutex == mutex);
    PRINT("lock through it", sceKernelLockMutex(opened_mutex, 1, NULL));
    PRINT("try lock through the mutex's own UID", sceKernelTryLockMutex(mutex, 1));
    PRINT("close it", sceKernelCloseMutex(opened_mutex));

    test_exit();
    return 0;
}
