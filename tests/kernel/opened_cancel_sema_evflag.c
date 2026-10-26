// Cancels a semaphore and an event flag through one of their two UIDs while a waiter queues through
// the other, then releases through the other UID. Each case makes the object OPENABLE, opens it by
// its name, and prints the result and the count the cancel wrote, the waiters the info call through
// the other UID counted just before the cancel, whether those two counts are equal, the waiter's
// result, and the result of the release. The model opened_waiters covers the waits through both UIDs,
// and the cancels here are new. The cases:
// - a semaphore, cancelled through the opened UID with the waiter queued through the creating UID,
//   then signaled through the creating UID
// - a semaphore, cancelled through the creating UID with the waiter queued through the opened UID,
//   then signaled through the opened UID
// - an event flag, cancelled through the opened UID with the set pattern 0x10 and the waiter queued
//   through the creating UID for the bit 0x1, then set 0x1 through the creating UID
// Every wait has a timeout of 2 seconds, and each waiter is joined before its result is read.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NAME "Cancel sync"
#define NOT_DONE 0xDEAD

enum Kind { SEMA, EVENT_FLAG };

static volatile int kind;
static SceUID wait_uid;
static volatile int wait_result;

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    unsigned int bits;
    switch (kind) {
    case SEMA: wait_result = sceKernelWaitSema(wait_uid, 1, &timeout); break;
    case EVENT_FLAG:
        wait_result = sceKernelWaitEventFlag(wait_uid, 0x1, SCE_EVENT_WAITOR, &bits, &timeout);
        break;
    }
    return 0;
}

static SceUID create(void) {
    switch (kind) {
    case SEMA: return sceKernelCreateSema(NAME, SCE_KERNEL_ATTR_OPENABLE, 0, 16, NULL);
    case EVENT_FLAG: return sceKernelCreateEventFlag(NAME, SCE_KERNEL_ATTR_OPENABLE, 0, NULL);
    }
    return -1;
}

static SceUID open_object(void) {
    switch (kind) {
    case SEMA: return sceKernelOpenSema(NAME);
    case EVENT_FLAG: return sceKernelOpenEventFlag(NAME);
    }
    return -1;
}

static int close_object(SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelCloseSema(id);
    case EVENT_FLAG: return sceKernelCloseEventFlag(id);
    }
    return -1;
}

static int delete_object(SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelDeleteSema(id);
    case EVENT_FLAG: return sceKernelDeleteEventFlag(id);
    }
    return -1;
}

// The waiters the info call through id counts, or its error
static int waiters(SceUID id) {
    SceKernelSemaInfo sema = {.size = sizeof(sema)};
    SceKernelEventFlagInfo flag = {.size = sizeof(flag)};
    int result;
    switch (kind) {
    case SEMA: result = sceKernelGetSemaInfo(id, &sema); return result < 0 ? result : sema.numWaitThreads;
    case EVENT_FLAG:
        result = sceKernelGetEventFlagInfo(id, &flag);
        return result < 0 ? result : (int)flag.numWaitThreads;
    }
    return -1;
}

// Cancels the waiters through id, and writes the count of them to waiting
static int cancel(SceUID id, int *waiting) {
    switch (kind) {
    case SEMA: return sceKernelCancelSema(id, 0, waiting);
    case EVENT_FLAG: return sceKernelCancelEventFlag(id, 0x10, (SceUInt32 *)waiting);
    }
    return -1;
}

// Releases the waiters of the object through id
static int release(SceUID id) {
    switch (kind) {
    case SEMA: return sceKernelSignalSema(id, 1);
    case EVENT_FLAG: return sceKernelSetEventFlag(id, 0x1);
    }
    return -1;
}

// Cancels through one UID while the waiter queues through the other, then releases through the other
static void run(const char *what, int cancel_through_opened) {
    const SceUID creating = create();
    const SceUID opened = open_object();
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "%s: create and open the object", what);
    const SceUID cancel_uid = cancel_through_opened ? opened : creating;
    const SceUID other = cancel_through_opened ? creating : opened;

    wait_uid = other;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(waiter);
    test_wait_until_waiting(thread);
    const int before = waiters(other);
    int waiting = NOT_DONE;
    const int canceled = cancel(cancel_uid, &waiting);
    test_join_thread(thread);
    PRINTF(canceled, "%s: the cancel", what);
    PRINTF(waiting, "%s: the waiters the cancel counted", what);
    PRINTF(before, "%s: the waiters the info counted before", what);
    PRINTF(waiting == before, "%s: the cancel's count equals the info's", what);
    PRINTF(wait_result, "%s: the waiter's result", what);
    PRINTF(release(other), "%s: the release through the other UID", what);

    close_object(opened);
    delete_object(creating);
}

int main(void) {
    kind = SEMA;
    run("semaphore, cancel through the opened UID, waiter through the creating UID", 1);
    run("semaphore, cancel through the creating UID, waiter through the opened UID", 0);
    kind = EVENT_FLAG;
    run("event flag, cancel through the opened UID, waiter through the creating UID", 1);
    test_exit();
    return 0;
}
