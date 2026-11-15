// Which kinds of objects share the process limit that object_limits and callback_limit reach. The
// test creates plain semaphores in a loop of at most 4096 until a create fails, and prints the
// error and whether the count is within bounds, so the output doesn't depend on how many objects
// the process holds when the test starts. While they are all there it creates an openable
// semaphore, a thread that never starts, and a callback, and deletes each one that was made. Then
// it deletes one semaphore, creates a callback in the room, and shows a plain semaphore doesn't
// fit next to it. Then it deletes the semaphores. Then it does the same with openable semaphores,
// each with a name of its own, and while they are all there it creates a plain semaphore and a
// callback, and deletes one openable semaphore to see the next openable create work and the one
// after fail. The results are printed after each phase's objects are deleted.
//
// On a PS Vita TV on firmware 3.65 a process may have 0x800 plain objects and 0x100 openable ones,
// counted separately. The test app already holds 16 plain and 20 openable objects when it starts,
// created by system modules, so 0x7EC plain semaphores fit and 0xEC openable ones, and a create
// past that fails with 0x8002450D (seen with an earlier form of this test that printed the
// counts).
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

// Deletes the semaphores fill made from index first on, and returns whether every delete worked
static int delete_all(int first, int count) {
    int all_deleted = 1;
    for (int i = first; i < count; i++)
        all_deleted &= sceKernelDeleteSema(uids[i]) == 0;
    return all_deleted;
}

static int try_sema(int openable) {
    // An index past every one fill and delete_one_create_one use
    const SceUID id = create_sema(openable ? LIMIT + 1 : -1);
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

// Deletes the semaphore uids[0], then creates one in its place and returns the result of the create
static int delete_one_create_one(int openable) {
    const int deleted = sceKernelDeleteSema(uids[0]);
    const SceUID id = create_sema(openable ? LIMIT : -1);
    uids[0] = id;
    return deleted == 0 ? test_created(id) : deleted;
}

int main(void) {
    // A line first, so the output file is open before the creates fill anything up
    const SceUID first = sceKernelCreateSema("first", 0, 0, 1, NULL);
    PRINT("create a semaphore before the others", test_created(first));
    sceKernelDeleteSema(first);

    int plain_error;
    const int plain = fill(0, &plain_error);
    const int plain_in_bounds = plain >= 0x7E0 && plain <= 0x800;
    const int openable_while_plain = try_sema(1);
    const int thread_while_plain = try_thread();
    const int callback_while_plain = try_callback();
    // The callback takes the room of one deleted semaphore, and then a semaphore doesn't fit
    const int deleted_plain = sceKernelDeleteSema(uids[0]);
    const SceUID kept_callback = sceKernelCreateCallback("limit source", 0, callback, NULL);
    const int callback_after_delete = test_created(kept_callback);
    const int plain_after_callback = try_sema(0);
    if (kept_callback >= 0)
        sceKernelDeleteCallback(kept_callback);
    const int plain_deleted = delete_all(1, plain);
    PRINT("plain semaphores: the error that stopped the fill", plain_error);
    PRINT("plain semaphores: the fill made at least 0x7E0 and at most 0x800", plain_in_bounds);
    PRINT("an openable semaphore while they are there", openable_while_plain);
    PRINT("a thread while they are there", thread_while_plain);
    PRINT("a callback while they are there", callback_while_plain);
    PRINT("delete one semaphore", deleted_plain);
    PRINT("a callback after deleting one", callback_after_delete);
    PRINT("a plain semaphore after that", plain_after_callback);
    PRINT("every other one deletes", plain_deleted);

    int openable_error;
    const int openable = fill(1, &openable_error);
    const int openable_in_bounds = openable >= 0xE0 && openable <= 0x100;
    const int plain_while_openable = try_sema(0);
    const int callback_while_openable = try_callback();
    const int one_openable = delete_one_create_one(1);
    const int another_openable = try_sema(1);
    const int openable_deleted = delete_all(0, openable);
    PRINT("openable semaphores: the error that stopped the fill", openable_error);
    PRINT("openable semaphores: the fill made at least 0xE0 and at most 0x100", openable_in_bounds);
    PRINT("a plain semaphore while they are there", plain_while_openable);
    PRINT("a callback while they are there", callback_while_openable);
    PRINT("an openable semaphore after deleting one", one_openable);
    PRINT("another openable semaphore after that", another_openable);
    PRINT("every one deletes", openable_deleted);

    test_exit();
    return 0;
}
