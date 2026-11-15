// How many semaphores, and then mutexes, a process can have at once. Each type is created in a loop
// of at most 4096 until a create fails, which prints the error and whether the count is within
// bounds, so the output doesn't depend on how many objects the process holds when the test starts.
// While the semaphores fill the limit, a mutex is created too, to see if the types share the limit.
// From the full state of each type one object is deleted, the next create works and the one after
// it fails. Then everything is deleted, and a create after that shows the room is back. The results
// are printed after the objects are deleted.
//
// On a PS Vita TV on firmware 3.65 a process may have 0x800 plain objects, counted over every type.
// The test app already holds 16 of them when it starts, created by system modules, so 0x7EC
// semaphores and 0x7EC mutexes fit, and a create past that fails with 0x8002450D (seen with an
// earlier form of this test that printed the counts).
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define LIMIT 4096

enum Kind { SEMA, MUTEX };
static const char *const kind_names[] = { "semaphores", "mutexes" };

static SceUID uids[LIMIT + 2];
static int count;

static SceUID create(int kind) {
    return kind == SEMA ? sceKernelCreateSema("limit", 0, 0, 1, NULL) : sceKernelCreateMutex("limit", 0, 0, NULL);
}

static int delete(int kind, SceUID id) {
    return kind == SEMA ? sceKernelDeleteSema(id) : sceKernelDeleteMutex(id);
}

// Creates an object of kind and keeps it when the create worked, and returns the result of the create
static int create_kept(int kind) {
    const SceUID id = create(kind);
    if (id >= 0)
        uids[count++] = id;
    return test_created(id);
}

int main(void) {
    // A line first, so the output file is open before the creates fill anything up
    const SceUID first = sceKernelCreateSema("first", 0, 0, 1, NULL);
    PRINT("create a semaphore before the others", test_created(first));
    sceKernelDeleteSema(first);

    for (int kind = SEMA; kind <= MUTEX; kind++) {
        const char *name = kind_names[kind];
        count = 0;
        int error = 0;
        while (count < LIMIT) {
            const SceUID id = create(kind);
            if (id < 0) {
                error = id;
                break;
            }
            uids[count++] = id;
        }
        const int in_bounds = count >= 0x7E0 && count <= 0x800;
        int mutex_result = 0;
        if (kind == SEMA) {
            const SceUID mutex = sceKernelCreateMutex("other", 0, 0, NULL);
            mutex_result = test_created(mutex);
            if (mutex >= 0)
                sceKernelDeleteMutex(mutex);
        }

        // One delete makes room for exactly one create
        // Nothing to delete when not even the first create worked
        if (count == 0)
            test_exit();
        const int delete_one = delete(kind, uids[--count]);
        const int after_delete = create_kept(kind);
        const int after_that = create_kept(kind);

        int all_deleted = 1;
        for (int i = 0; i < count; i++)
            all_deleted &= delete(kind, uids[i]) == 0;
        const SceUID again = create(kind);
        const int again_result = test_created(again);
        if (again >= 0)
            delete(kind, again);

        PRINTF(error, "%s: the error that stopped the fill", name);
        PRINTF(in_bounds, "%s: the fill made at least 0x7E0 and at most 0x800", name);
        if (kind == SEMA)
            PRINTF(mutex_result, "%s: create a mutex while they are there", name);
        PRINTF(delete_one, "%s: delete one", name);
        PRINTF(after_delete, "%s: create one after deleting one", name);
        PRINTF(after_that, "%s: create another after that", name);
        PRINTF(all_deleted, "%s: every one deletes", name);
        PRINTF(again_result, "%s: create one after deleting them", name);
    }

    test_exit();
    return 0;
}
