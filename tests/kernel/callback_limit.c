// How many callbacks a process can have at once, and what gives their room back. Callbacks are made
// in a loop of at most 4096 until one create fails, which prints the count reached and the error,
// as object_limits does for semaphores and mutexes. The cases, in order:
// - the main thread makes callbacks until the limit, then a semaphore is created, to see if the
//   limit is shared with the other kinds
// - a second thread, started before the limit was reached, makes one callback, to see if the limit
//   is per thread or per process
// - every callback is deleted, and the main thread makes callbacks until the limit again
// - the main thread deletes them again, and a third thread makes callbacks until the limit and ends
//   without deleting them, then the main thread makes callbacks until the limit again
// - one callback is deleted and another created, to see if the new UID is the old one
// The loops keep the UIDs of the callbacks the main thread made, and every one is deleted before
// the test ends.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LIMIT 4096

static SceUID uids[LIMIT], thread_uids[LIMIT];
static SceUID gate;
static int second_result, filler_error;
static int filler_count;

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Makes callbacks into ids until one create fails or the limit is reached, and returns how many it
// made. The error of the failed create is written to error.
static int make_callbacks(SceUID *ids, int *error) {
    int count = 0;
    while (count < LIMIT) {
        const SceUID id = sceKernelCreateCallback("limit", 0, callback, NULL);
        if (id < 0) {
            *error = id;
            break;
        }
        ids[count++] = id;
    }
    return count;
}

// Waits for the gate, then makes one callback and deletes it again
static int second_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 5 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    const SceUID id = sceKernelCreateCallback("second", 0, callback, NULL);
    second_result = test_created(id);
    if (id >= 0)
        sceKernelDeleteCallback(id);
    return 0;
}

// Makes callbacks until the limit and ends without deleting them, so they go with the thread
static int filler_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    filler_count = make_callbacks(thread_uids, &filler_error);
    return 0;
}

// Deletes the callbacks in ids, and returns whether every delete succeeded
static int delete_all(const SceUID *ids, int count) {
    int all_deleted = 1;
    for (int i = 0; i < count; i++)
        all_deleted &= sceKernelDeleteCallback(ids[i]) == 0;
    return all_deleted;
}

int main(void) {
    // A line first, so the output file is open before the creates fill anything up
    const SceUID first = sceKernelCreateCallback("first", 0, callback, NULL);
    PRINT("create a callback before the others", test_created(first));
    sceKernelDeleteCallback(first);

    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID second = test_start_thread(second_thread);

    int error = 0;
    const int count = make_callbacks(uids, &error);
    const SceUID sema = sceKernelCreateSema("limit", 0, 0, 1, NULL);
    const int sema_result = test_created(sema);
    if (sema >= 0)
        sceKernelDeleteSema(sema);

    // The thread is released only now, and the results are printed once it has ended
    sceKernelSignalSema(gate, 1);
    test_join_thread(second);
    PRINTF(count, "callbacks the main thread created before an error or the limit");
    PRINTF(error, "the error");
    PRINTF(sema_result, "create a semaphore while they are there");
    PRINTF(second_result, "create a callback from another thread while they are there");

    PRINTF(delete_all(uids, count), "every one deletes");

    // The room is back after the deletes, and the limit is reached again
    error = 0;
    const int again = make_callbacks(uids, &error);
    PRINTF(again, "callbacks the main thread created after deleting them");
    PRINTF(error, "the error after deleting them");
    PRINTF(delete_all(uids, again), "every one deletes the second time");

    // A third thread makes callbacks until the limit, then ends with them
    const SceUID filler = test_start_thread(filler_thread);
    test_join_thread(filler);
    PRINTF(filler_count, "callbacks a thread created before an error or the limit");
    PRINTF(filler_error, "the error of the thread");

    error = 0;
    const int after = make_callbacks(uids, &error);
    PRINTF(after, "callbacks the main thread created after the thread ended");
    PRINTF(error, "the error after the thread ended");

    // A deleted callback's UID, and the one created after it
    const SceUID old = uids[0];
    PRINT("delete one", sceKernelDeleteCallback(old));
    const SceUID new_one = sceKernelCreateCallback("new", 0, callback, NULL);
    PRINT("create one after it", test_created(new_one));
    PRINT("the new UID is the deleted one's", new_one == old);
    if (new_one >= 0)
        sceKernelDeleteCallback(new_one);
    PRINTF(delete_all(uids + 1, after - 1), "every other one deletes");
    sceKernelDeleteSema(gate);

    test_exit();
    return 0;
}
