// Which error a semaphore call reports when its UID is not a semaphore and its counts are wrong
// too, and which error creation reports first when its arguments are wrong. This is a probe: every
// result is printed as it comes, so the output is whatever order the implementation checks in. The
// cases:
// - a UID that is unknown, deleted, 0 or -1, the UID of a mutex and the UID of an event flag. Each
//   gets a wait, a wait with callbacks and a poll for counts 0 and -1, a signal of -1 and 0, and a
//   cancel to 1000000.
// - a semaphore with a maximum of 2 and a count of 1, with a wait, a wait with callbacks and a poll
//   for -1 and for 0x80000000. The count left after them is printed.
// - creation with a bad attribute 0x4, a count above the maximum, and an option struct whose size
//   is 0, 4, 5 or 8, in pairs of wrong arguments. Creation with a NULL name goes last.
// Every semaphore made by the cases is deleted right after its result is printed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define UNKNOWN_UID 0x12345

// The timeout of the waits, short since none of them should queue
#define WAIT_TIMEOUT_US 1000

// An option struct padded to 8 bytes, whose size field each case sets
typedef struct OptParam {
    SceSize size;
    SceUInt32 reserved;
} OptParam;

// Makes a wait, a wait with callbacks, a poll, a signal and a cancel on uid, and prints each result
static void uid_calls(const char *name, SceUID uid) {
    SceUInt timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSema(uid, 0, &timeout), "%s: wait for 0", name);
    timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSema(uid, -1, &timeout), "%s: wait for -1", name);
    timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSemaCB(uid, 0, &timeout), "%s: wait with callbacks for 0", name);
    timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSemaCB(uid, -1, &timeout), "%s: wait with callbacks for -1", name);
    PRINTF(sceKernelPollSema(uid, 0), "%s: poll for 0", name);
    PRINTF(sceKernelPollSema(uid, -1), "%s: poll for -1", name);
    PRINTF(sceKernelSignalSema(uid, -1), "%s: signal -1", name);
    PRINTF(sceKernelSignalSema(uid, 0), "%s: signal 0", name);
    int waiting = NOT_DONE;
    PRINTF(sceKernelCancelSema(uid, 1000000, &waiting), "%s: cancel to 1000000", name);
}

// Makes a wait, a wait with callbacks and a poll on sema for count, and prints each result
static void sema_calls(const char *name, SceUID sema, int count) {
    SceUInt timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSema(sema, count, &timeout), "%s: wait", name);
    timeout = WAIT_TIMEOUT_US;
    PRINTF(sceKernelWaitSemaCB(sema, count, &timeout), "%s: wait with callbacks", name);
    PRINTF(sceKernelPollSema(sema, count), "%s: poll", name);
}

// Prints the result of a create call, and deletes the semaphore it made
static void created(const char *name, SceUID sema) {
    PRINTF(test_created(sema), "%s", name);
    if (sema >= 0)
        sceKernelDeleteSema(sema);
}

int main(void) {
    const SceUID deleted = sceKernelCreateSema("deleted", 0, 0, 1, NULL);
    sceKernelDeleteSema(deleted);
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    const SceUID ids[] = { UNKNOWN_UID, deleted, 0, -1, mutex, flag };
    const char *const names[] = {
        "an unknown UID", "a deleted semaphore", "UID 0", "UID -1", "a mutex", "an event flag"
    };
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        uid_calls(names[i], ids[i]);

    // A real semaphore, which the bad counts leave alone
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 2, NULL);
    sema_calls("semaphore at 1 of 2, count -1", sema, -1);
    sema_calls("semaphore at 1 of 2, count 0x80000000", sema, (int)0x80000000);
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    PRINT("semaphore at 1 of 2, its count after the bad counts", info.currentCount);
    sceKernelDeleteSema(sema);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteEventFlag(flag);

    // Creation, where the first wrong argument in the order of the checks is the error
    OptParam opt0 = { .size = 0 };
    OptParam opt4 = { .size = 4 };
    OptParam opt5 = { .size = 5 };
    OptParam opt8 = { .size = 8 };
    created("create with attribute 0x4 and count 2 of 1",
        sceKernelCreateSema("sema", 0x4, 2, 1, NULL));
    created("create with attribute 0x4 and option size 8",
        sceKernelCreateSema("sema", 0x4, 0, 1, (SceKernelSemaOptParam *)&opt8));
    created("create with count 2 of 1 and option size 8",
        sceKernelCreateSema("sema", 0, 2, 1, (SceKernelSemaOptParam *)&opt8));
    created("create with option size 0",
        sceKernelCreateSema("sema", 0, 0, 1, (SceKernelSemaOptParam *)&opt0));
    created("create with option size 4",
        sceKernelCreateSema("sema", 0, 0, 1, (SceKernelSemaOptParam *)&opt4));
    created("create with option size 5",
        sceKernelCreateSema("sema", 0, 0, 1, (SceKernelSemaOptParam *)&opt5));
    created("create with option size 8",
        sceKernelCreateSema("sema", 0, 0, 1, (SceKernelSemaOptParam *)&opt8));

    // NULL names last, so an implementation that crashes on them loses nothing else
    created("create with a NULL name and attribute 0x4",
        sceKernelCreateSema(NULL, 0x4, 0, 1, NULL));
    created("create with a NULL name and count 2 of 1",
        sceKernelCreateSema(NULL, 0, 2, 1, NULL));

    test_exit();
    return 0;
}
