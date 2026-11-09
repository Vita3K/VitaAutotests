// Which kinds of objects share the process limit that object_limits and callback_limit reach. The
// test creates plain semaphores in a loop of at most 4096 until a create fails, and prints the
// count reached and the error. While they are all there it creates an openable semaphore, a thread
// that never starts, and a callback, and deletes each one that was made. Then it deletes the
// semaphores. Then it does the same with openable semaphores, each with a name of its own, and
// while they are all there it creates a plain semaphore and a callback. The counts depend on the
// objects the process already has, the same on every run. The results are printed after each
// phase's objects are deleted.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LIMIT 4096

static SceUID uids[LIMIT];

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

// Creates a semaphore, openable with a name of its own when index >= 0
static SceUID create_sema(int index) {
    if (index < 0)
        return sceKernelCreateSema("limit source", 0, 0, 1, NULL);
    char name[32];
    sceClibSnprintf(name, sizeof(name), "limit source %04d", index);
    return sceKernelCreateSema(name, SCE_KERNEL_ATTR_OPENABLE, 0, 1, NULL);
}

// Creates semaphores until one fails or the loop ends, and returns how many it made. The error of
// the failed create is written to error.
static int fill(int openable, int *error) {
    int count = 0;
    *error = 0;
    while (count < LIMIT) {
        const SceUID id = create_sema(openable ? count : -1);
        if (id < 0) {
            *error = id;
            break;
        }
        uids[count++] = id;
    }
    return count;
}

// Deletes the semaphores fill made, and returns whether every delete worked
static int delete_all(int count) {
    int all_deleted = 1;
    for (int i = 0; i < count; i++)
        all_deleted &= sceKernelDeleteSema(uids[i]) == 0;
    return all_deleted;
}

static int try_sema(int openable) {
    // An index past every one fill uses
    const SceUID id = create_sema(openable ? LIMIT : -1);
    if (id >= 0)
        sceKernelDeleteSema(id);
    return test_created(id);
}

static int try_thread(void) {
    const SceUID id
        = sceKernelCreateThread("limit source", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x1000, 0, 0, NULL);
    if (id >= 0)
        sceKernelDeleteThread(id);
    return test_created(id);
}

static int try_callback(void) {
    const SceUID id = sceKernelCreateCallback("limit source", 0, callback, NULL);
    if (id >= 0)
        sceKernelDeleteCallback(id);
    return test_created(id);
}

int main(void) {
    // A line first, so the output file is open before the creates fill anything up
    const SceUID first = sceKernelCreateSema("first", 0, 0, 1, NULL);
    PRINT("create a semaphore before the others", test_created(first));
    sceKernelDeleteSema(first);

    int plain_error;
    const int plain = fill(0, &plain_error);
    const int openable_while_plain = try_sema(1);
    const int thread_while_plain = try_thread();
    const int callback_while_plain = try_callback();
    const int plain_deleted = delete_all(plain);
    PRINT("plain semaphores created before an error or the end of the loop", plain);
    PRINT("the error", plain_error);
    PRINT("an openable semaphore while they are there", openable_while_plain);
    PRINT("a thread while they are there", thread_while_plain);
    PRINT("a callback while they are there", callback_while_plain);
    PRINT("every one deletes", plain_deleted);

    int openable_error;
    const int openable = fill(1, &openable_error);
    const int plain_while_openable = try_sema(0);
    const int callback_while_openable = try_callback();
    const int openable_deleted = delete_all(openable);
    PRINT("openable semaphores created before an error or the end of the loop", openable);
    PRINT("the error", openable_error);
    PRINT("a plain semaphore while they are there", plain_while_openable);
    PRINT("a callback while they are there", callback_while_openable);
    PRINT("every one deletes", openable_deleted);

    test_exit();
    return 0;
}
