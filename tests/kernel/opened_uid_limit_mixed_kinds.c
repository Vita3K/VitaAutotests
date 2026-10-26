// The open limit for opened UIDs, with other kinds opened past it. The test makes an openable
// semaphore, event flag and mutex, then opens the semaphore until an open fails, at most 4096
// times. The cases:
// - whether the opens reached the end of the loop, and the first error. The number of opens
//   depends on the objects the process already has, so it isn't printed
// - an open of the event flag and of the mutex past that limit, each result as it comes
// - a close of one opened semaphore UID, then an open of the event flag, and whether the open got
//   the freed slot
// Then every opened UID is closed and the three objects are deleted. The model open_uid_limits
// fills the limit with the semaphore alone, then checks the creating UID and a later open. This
// test is a test of its own, since a wrong implementation may strain under the opens.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The most opens the test tries
#define LIMIT 4096

static SceUID uids[LIMIT];

int main(void) {
    const SceUID sema = sceKernelCreateSema("opened", SCE_KERNEL_ATTR_OPENABLE, 0, 4, NULL);
    const SceUID flag = sceKernelCreateEventFlag("opened flag", SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    const SceUID mutex = sceKernelCreateMutex("opened mutex", SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    PRINT("create the openable semaphore", test_created(sema));
    PRINT("create the openable event flag", test_created(flag));
    PRINT("create the openable mutex", test_created(mutex));

    int opens = 0, error = 0;
    while (opens < LIMIT) {
        const SceUID id = sceKernelOpenSema("opened");
        if (id < 0) {
            error = id;
            break;
        }
        uids[opens++] = id;
    }
    PRINT("the opens reached the end of the loop", opens == LIMIT);
    PRINT("the first open that failed", error);

    const SceUID flag_opened = sceKernelOpenEventFlag("opened flag");
    PRINT("open the event flag past the limit", test_created(flag_opened));
    const SceUID mutex_opened = sceKernelOpenMutex("opened mutex");
    PRINT("open the mutex past the limit", test_created(mutex_opened));

    // Closes the semaphore UID the first open gave, so the event flag can take its slot
    int closed_first = -1;
    if (opens > 0)
        closed_first = sceKernelCloseSema(uids[0]);
    PRINT("close one opened semaphore UID", closed_first);
    const SceUID flag_again = sceKernelOpenEventFlag("opened flag");
    PRINT("open the event flag after the close", test_created(flag_again));
    PRINT("the freed slot is taken by the event flag", flag_again >= 0);

    int all_closed = 1;
    for (int i = 1; i < opens; i++)
        all_closed &= sceKernelCloseSema(uids[i]) == 0;
    if (flag_opened >= 0)
        all_closed &= sceKernelCloseEventFlag(flag_opened) == 0;
    if (mutex_opened >= 0)
        all_closed &= sceKernelCloseMutex(mutex_opened) == 0;
    if (flag_again >= 0)
        all_closed &= sceKernelCloseEventFlag(flag_again) == 0;
    PRINT("every other opened UID closes", all_closed);

    PRINT("delete the openable semaphore", sceKernelDeleteSema(sema));
    PRINT("delete the openable event flag", sceKernelDeleteEventFlag(flag));
    PRINT("delete the openable mutex", sceKernelDeleteMutex(mutex));

    test_exit();
    return 0;
}
