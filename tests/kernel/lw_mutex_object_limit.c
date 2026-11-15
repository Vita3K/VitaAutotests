// Whether lightweight mutexes share the object limit of a process with semaphores and mutexes. As
// in object_limits, each type is created in a loop of at most 4096 until a create fails, and the
// error and whether the count is within bounds are printed, so the output doesn't depend on how
// many objects the process holds when the test starts. The lightweight mutexes each get a static
// work area, which a failed create must leave alone. Then, while the lightweight mutexes fill the
// limit, a semaphore and a mutex are created, and deleting one lightweight mutex makes room for
// one semaphore and not for a second one. Deleting all of them makes room for lightweight mutexes
// again. Last, the limit is filled with semaphores, one lightweight mutex is created while they
// are there, deleting one semaphore makes room for one lightweight mutex and not for a semaphore
// next to it, and the count of semaphores reached is compared with the count of lightweight
// mutexes. The cases:
// - the error that stopped the lightweight mutexes, whether the count is within bounds, and
//   whether the area of the failed create is unchanged
// - while they fill the limit, a semaphore, a mutex and another lightweight mutex
// - a semaphore after one lightweight mutex is deleted, and a second semaphore next to it
// - every other lightweight mutex deletes, and a lightweight mutex is created after that
// - the error that stopped the semaphores, and whether the count is within bounds
// - a lightweight mutex while the semaphores fill the limit, and whether its area is unchanged
// - a lightweight mutex after one semaphore is deleted, and a semaphore next to it
// - every semaphore deletes, and the semaphore count equals the lightweight mutex count
//
// On a PS Vita TV on firmware 3.65 a process may have 0x800 plain objects, counted over every type.
// The test app already holds 16 of them when it starts, created by system modules, so 0x7EC
// lightweight mutexes and 0x7EC semaphores fit, and a create past that fails with 0x8002450D
// (seen with an earlier form of this test that printed the counts).
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LIMIT 4096
#define FILL 0xEE

// The work areas of the lightweight mutexes, with a spare one past the limit for the failed create,
// and the semaphores
static SceKernelLwMutexWork works[LIMIT + 1];
static SceUID semas[LIMIT];

// Whether the work area still holds the fill value in every byte
static int still_filled(const SceKernelLwMutexWork *work) {
    const unsigned char *bytes = (const unsigned char *)work;
    int same = 1;
    for (unsigned int i = 0; i < sizeof(*work); i++)
        same &= bytes[i] == FILL;
    return same;
}

// Creates a lightweight mutex named limit in work, deletes it if the create worked, and returns the
// result of the create
static int create_and_delete(SceKernelLwMutexWork *work) {
    const int result = sceKernelCreateLwMutex(work, "limit", 0, 0, NULL);
    if (result == 0)
        sceKernelDeleteLwMutex(work);
    return result;
}

int main(void) {
    sceClibMemset(works, FILL, sizeof(works));

    // A line first, so the output file is open before the creates fill anything up
    PRINT("create a lightweight mutex before the others",
        test_created(create_and_delete(&works[LIMIT - 1])));

    int count = 0, error = 0;
    while (count < LIMIT) {
        const int result = sceKernelCreateLwMutex(&works[count], "limit", 0, 0, NULL);
        if (result != 0) {
            error = result;
            break;
        }
        count++;
    }
    PRINT("lightweight mutexes: the error that stopped the fill", error);
    PRINT("lightweight mutexes: the fill made at least 0x7E0 and at most 0x800",
        count >= 0x7E0 && count <= 0x800);
    PRINT("lightweight mutexes: the area of the failed create is unchanged",
        still_filled(&works[count]));

    const SceUID sema_while_full = sceKernelCreateSema("limit", 0, 0, 1, NULL);
    PRINT("lightweight mutexes: a semaphore while they are there", test_created(sema_while_full));
    if (sema_while_full >= 0)
        sceKernelDeleteSema(sema_while_full);
    const SceUID mutex_while_full = sceKernelCreateMutex("limit", 0, 0, NULL);
    PRINT("lightweight mutexes: a mutex while they are there", test_created(mutex_while_full));
    if (mutex_while_full >= 0)
        sceKernelDeleteMutex(mutex_while_full);
    PRINT("lightweight mutexes: another lightweight mutex while they are there",
        create_and_delete(&works[count]));
    PRINT("lightweight mutexes: the area of that create is unchanged",
        still_filled(&works[count]));

    PRINT("delete one lightweight mutex", sceKernelDeleteLwMutex(&works[0]));
    const SceUID sema_after_one = sceKernelCreateSema("limit", 0, 0, 1, NULL);
    PRINT("a semaphore after deleting one lightweight mutex", test_created(sema_after_one));
    const SceUID sema_next_to_it = sceKernelCreateSema("limit", 0, 0, 1, NULL);
    PRINT("a second semaphore next to it", test_created(sema_next_to_it));
    if (sema_next_to_it >= 0)
        sceKernelDeleteSema(sema_next_to_it);
    if (sema_after_one >= 0)
        sceKernelDeleteSema(sema_after_one);

    int all_deleted = 1;
    for (int i = 1; i < count; i++)
        all_deleted &= sceKernelDeleteLwMutex(&works[i]) == 0;
    PRINT("every other lightweight mutex deletes", all_deleted);
    PRINT("create a lightweight mutex after deleting them", create_and_delete(&works[0]));

    int sema_count = 0, sema_error = 0;
    while (sema_count < LIMIT) {
        const SceUID id = sceKernelCreateSema("limit", 0, 0, 1, NULL);
        if (id < 0) {
            sema_error = id;
            break;
        }
        semas[sema_count++] = id;
    }
    PRINT("semaphores: the error that stopped the fill", sema_error);
    PRINT("semaphores: the fill made at least 0x7E0 and at most 0x800",
        sema_count >= 0x7E0 && sema_count <= 0x800);

    sceClibMemset(&works[0], FILL, sizeof(works[0]));
    PRINT("a lightweight mutex while the semaphores are there", create_and_delete(&works[0]));
    PRINT("a lightweight mutex while the semaphores are there: the area is unchanged",
        still_filled(&works[0]));

    // One deleted semaphore makes room for one lightweight mutex, and then a semaphore doesn't fit
    // Nothing to delete when not even the first create worked
    if (sema_count == 0)
        test_exit();
    int kept = sema_count - 1;
    PRINT("delete one semaphore", sceKernelDeleteSema(semas[kept]));
    const int lw_after_delete = sceKernelCreateLwMutex(&works[0], "limit", 0, 0, NULL);
    PRINT("a lightweight mutex after deleting one semaphore", test_created(lw_after_delete));
    const SceUID sema_next_to_lw = sceKernelCreateSema("limit", 0, 0, 1, NULL);
    PRINT("a semaphore next to it", test_created(sema_next_to_lw));
    if (sema_next_to_lw >= 0)
        semas[kept++] = sema_next_to_lw;
    if (lw_after_delete == 0)
        sceKernelDeleteLwMutex(&works[0]);

    int all_semas_deleted = 1;
    for (int i = 0; i < kept; i++)
        all_semas_deleted &= sceKernelDeleteSema(semas[i]) == 0;
    PRINT("every semaphore deletes", all_semas_deleted);
    PRINT("the semaphore count equals the lightweight mutex count", sema_count == count);

    test_exit();
    return 0;
}
