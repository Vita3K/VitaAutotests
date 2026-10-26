// Whether lightweight mutexes share the object limit of a process with semaphores and mutexes. As
// in object_limits, each type is created in a loop of at most 4096 until a create fails, and the
// count reached and the error are printed. The lightweight mutexes each get a static work area,
// which a failed create must leave alone. Then, while the lightweight mutexes fill the limit, a
// semaphore and a mutex are created, and deleting one lightweight mutex makes room for a semaphore.
// Deleting all of them makes room for lightweight mutexes again. Last, the limit is filled with
// semaphores, one lightweight mutex is created while they are there, and the count of semaphores
// reached is compared with the count of lightweight mutexes. The cases:
// - the lightweight mutexes created before an error, the error, and whether the area of the failed
//   create is unchanged
// - while they fill the limit, a semaphore, a mutex and another lightweight mutex
// - a semaphore after one lightweight mutex is deleted
// - every other lightweight mutex deletes, and a lightweight mutex is created after that
// - the semaphores created before an error, and the error
// - a lightweight mutex while the semaphores fill the limit, and whether its area is unchanged
// - every semaphore deletes, and the semaphore count equals the lightweight mutex count
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
    PRINT("lightweight mutexes: created before an error or the end of the loop", count);
    PRINT("lightweight mutexes: the error", error);
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
    PRINT("semaphores: created before an error or the end of the loop", sema_count);
    PRINT("semaphores: the error", sema_error);

    sceClibMemset(&works[0], FILL, sizeof(works[0]));
    PRINT("a lightweight mutex while the semaphores are there", create_and_delete(&works[0]));
    PRINT("a lightweight mutex while the semaphores are there: the area is unchanged",
        still_filled(&works[0]));

    int all_semas_deleted = 1;
    for (int i = 0; i < sema_count; i++)
        all_semas_deleted &= sceKernelDeleteSema(semas[i]) == 0;
    PRINT("every semaphore deletes", all_semas_deleted);
    PRINT("the semaphore count equals the lightweight mutex count", sema_count == count);

    test_exit();
    return 0;
}
