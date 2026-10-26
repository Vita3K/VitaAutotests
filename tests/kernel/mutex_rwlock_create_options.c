// Which option sizes mutex and rwlock creation accepts, and which error a create reports when its
// option size, attribute, initial count or ceiling is wrong. Each create gets an option struct of 16
// bytes that is zero except for its size and, in the ceiling cases, the ceiling priority. Each case
// prints the result of its create, and an object that was created is deleted at once. The cases:
// - mutexes with option sizes 0, 4, 8, 9 and 12
// - mutexes with option size 12 and the attribute 0x80000000, with an initial count of 2 on a plain
//   mutex, and with the ceiling attribute 0x4 and the ceiling 0x100, then the same count and ceiling
//   with option size 8
// - rwlocks with option sizes 0, 4, 5 and 8, then option size 8 with the attribute 0x80000000
// - the option size 0xFFFFFFFF of a mutex and of a rwlock, last of all
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// An option struct of 16 bytes, which is larger than any option size the cases pass
typedef struct OptParam {
    SceSize size;
    int ceilingPriority;
    SceUInt32 reserved[2];
} OptParam;

typedef struct MutexCase {
    const char *name;
    SceUInt32 attr;
    SceSize size;
    int count;
    int ceiling;
} MutexCase;

typedef struct RwlockCase {
    const char *name;
    SceUInt32 attr;
    SceSize size;
} RwlockCase;

static const MutexCase mutexes[] = {
    { "mutex, option size 0", 0, 0, 0, 0 },
    { "mutex, option size 4", 0, 4, 0, 0 },
    { "mutex, option size 8", 0, 8, 0, 0 },
    { "mutex, option size 9", 0, 9, 0, 0 },
    { "mutex, option size 12", 0, 12, 0, 0 },
    { "mutex, option size 12, attribute 0x80000000", 0x80000000, 12, 0, 0 },
    { "mutex, option size 12, initial count 2, plain", 0, 12, 2, 0 },
    { "mutex, option size 12, ceiling attribute 0x4, ceiling 0x100", SCE_KERNEL_MUTEX_ATTR_CEILING, 12, 0, 0x100 },
    { "mutex, option size 8, initial count 2, plain", 0, 8, 2, 0 },
    { "mutex, option size 8, ceiling attribute 0x4, ceiling 0x100", SCE_KERNEL_MUTEX_ATTR_CEILING, 8, 0, 0x100 },
};

static const RwlockCase rwlocks[] = {
    { "rwlock, option size 0", 0, 0 },
    { "rwlock, option size 4", 0, 4 },
    { "rwlock, option size 5", 0, 5 },
    { "rwlock, option size 8", 0, 8 },
    { "rwlock, option size 8, attribute 0x80000000", 0x80000000, 8 },
};

static const MutexCase mutex_huge = { "mutex, option size 0xFFFFFFFF", 0, 0xFFFFFFFF, 0, 0 };
static const RwlockCase rwlock_huge = { "rwlock, option size 0xFFFFFFFF", 0, 0xFFFFFFFF };

// Creates a mutex with the case's options, prints the result, and deletes it if it was created
static void create_mutex(const MutexCase *c) {
    OptParam opt = { .size = c->size, .ceilingPriority = c->ceiling };
    const SceUID mutex = sceKernelCreateMutex("mutex", c->attr, c->count, (SceKernelMutexOptParam *)&opt);
    PRINTF(test_created(mutex), "%s: create", c->name);
    if (mutex >= 0)
        sceKernelDeleteMutex(mutex);
}

// Creates a rwlock with the case's options, prints the result, and deletes it if it was created
static void create_rwlock(const RwlockCase *c) {
    OptParam opt = { .size = c->size };
    const SceUID rwlock = sceKernelCreateRWLock("rwlock", c->attr, (const SceKernelRWLockOptParam *)&opt);
    PRINTF(test_created(rwlock), "%s: create", c->name);
    if (rwlock >= 0)
        sceKernelDeleteRWLock(rwlock);
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(mutexes) / sizeof(mutexes[0]); i++)
        create_mutex(&mutexes[i]);
    for (unsigned int i = 0; i < sizeof(rwlocks) / sizeof(rwlocks[0]); i++)
        create_rwlock(&rwlocks[i]);

    // The largest option sizes last, in case an implementation reads past the option struct
    create_mutex(&mutex_huge);
    create_rwlock(&rwlock_huge);

    test_exit();
    return 0;
}
