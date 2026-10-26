// Lightweight mutex calls on work areas that the test builds by hand. Every call gets an area of
// its own, built for its case: the owner, count and UID words are set, the attribute is 0 (not
// recursive) and the other words are 0. The calls read the owner, count and UID words, and the
// kernel finds the mutex by the UID word, so the result depends on which object that word names.
// The cases are every pair of:
// - the owner word: 0 (free), the caller's UID with a count of 1 (held by caller), or the UID of
//   another thread, which waits on a semaphore for the whole test (held by other)
// - the UID word: 0, -1, 0x12345 (names no object), the UID of a live lightweight mutex with a work
//   area of its own, the UID of a deleted lightweight mutex, or the UID of a live semaphore
// Each case makes one call: try lock with a count of 1, lock with a count of 1 and a timeout of 0,
// lock with a count of 0 and a timeout of 0, unlock with a count of 1, unlock 2 with a count of 1,
// delete, or info by work area. Each call prints its result and whether words 0 to 2 of its area
// changed. Info by work area reads the object that the UID word names, so for the live mutex it
// prints that mutex's own work area, owner and count, which are free and 0 since no call locks it.
// The semaphore row goes last, since a wrong implementation may crash on a UID of another class.
// lw_mutex_user_paths covers unlock with the UID words 0 and -1 on a zeroed area, and this
// extends it.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum { OWNER_FREE, OWNER_CALLER, OWNER_OTHER, OWNER_COUNT };
enum {
    UID_ZERO,
    UID_MINUS_ONE,
    UID_NO_OBJECT,
    UID_LIVE_MUTEX,
    UID_DELETED,
    UID_SEMAPHORE,
    UID_COUNT
};
enum {
    CALL_TRY_LOCK,
    CALL_LOCK,
    CALL_LOCK_ZERO,
    CALL_UNLOCK,
    CALL_UNLOCK2,
    CALL_DELETE,
    CALL_INFO,
    CALL_COUNT
};

static const char *const owner_names[OWNER_COUNT] = { "free", "held by caller", "held by other" };
static const char *const uid_names[UID_COUNT] = {
    "UID 0",
    "UID -1",
    "UID of no object",
    "UID of a live mutex",
    "UID of a deleted mutex",
    "UID of a live semaphore",
};
static const char *const call_names[CALL_COUNT] = {
    "try lock of 1",
    "lock of 1 timeout 0",
    "lock of 0 timeout 0",
    "unlock of 1",
    "unlock 2 of 1",
    "delete",
    "info by work area",
};

// The mutex with a work area of its own. No call locks it, so it stays free.
static SceKernelLwMutexWork live_mutex;
// One area per case and call, so no call sees words that another one wrote
static SceKernelLwMutexWork areas[UID_COUNT][OWNER_COUNT][CALL_COUNT];
static unsigned int uid_word[UID_COUNT];
static SceUID gate;
// The user UID of the parked thread, which the areas of the other owner hold
static volatile unsigned int other_id;

// Records its UID and waits on the gate, so it stays parked for the whole test
static int parked_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    other_id = (unsigned int)sceKernelGetThreadId();
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// The owner word for a case
static unsigned int owner_word(int owner) {
    if (owner == OWNER_CALLER)
        return (unsigned int)sceKernelGetThreadId();
    if (owner == OWNER_OTHER)
        return other_id;
    return 0;
}

// Sets the owner, count and UID words of a work area for a case, and the rest to 0
static void build_area(SceKernelLwMutexWork *work, int owner, unsigned int uid) {
    volatile unsigned int *words = (volatile unsigned int *)work;
    words[0] = owner_word(owner);
    words[1] = 0;
    words[2] = owner == OWNER_FREE ? 0 : 1;
    words[3] = 0;
    words[4] = uid;
    words[5] = 0;
    words[6] = 0;
    words[7] = 0;
}

// Copies words 0 to 2 of a work area
static void read_words(const SceKernelLwMutexWork *work, unsigned int *words) {
    const volatile unsigned int *in = (const volatile unsigned int *)work;
    for (int i = 0; i < 3; i++)
        words[i] = in[i];
}

// Returns whether any of words 0 to 2 differ
static int words_differ(const unsigned int *a, const unsigned int *b) {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i])
            return 1;
    }
    return 0;
}

// Makes one call on work and returns its result, with info filled for the info call
static int make_call(int call, SceKernelLwMutexWork *work, LwMutexInfo *info) {
    SceUInt32 timeout = 0;
    switch (call) {
    case CALL_TRY_LOCK:
        return sceKernelTryLockLwMutex(work, 1);
    case CALL_LOCK:
        return sceKernelLockLwMutex(work, 1, &timeout);
    case CALL_LOCK_ZERO:
        return sceKernelLockLwMutex(work, 0, &timeout);
    case CALL_UNLOCK:
        return sceKernelUnlockLwMutex(work, 1);
    case CALL_UNLOCK2:
        return sceKernelUnlockLwMutex2(work, 1);
    case CALL_DELETE:
        return sceKernelDeleteLwMutex(work);
    default:
        return sceKernelGetLwMutexInfo(work, info);
    }
}

// Runs one call on a fresh area built for the owner and the UID word, and prints the results
static void run_case(int uid, int owner, int call) {
    SceKernelLwMutexWork *work = &areas[uid][owner][call];
    build_area(work, owner, uid_word[uid]);
    unsigned int before[3], after[3];
    read_words(work, before);
    LwMutexInfo info = { .size = sizeof(info) };
    const int result = make_call(call, work, &info);
    read_words(work, after);

    char label[96];
    sceClibSnprintf(label, sizeof(label), "%s, %s: %s", owner_names[owner], uid_names[uid],
                    call_names[call]);
    PRINTF(result, "%s", label);
    PRINTF(words_differ(before, after), "%s changes words 0 to 2", label);
    if (call == CALL_INFO && result == 0) {
        PRINTF(info.uid == (SceUID)uid_word[uid], "%s: the info uid is the UID word", label);
        PRINTF(info.work == &live_mutex, "%s: the info work area is the live mutex's", label);
        PRINTF(info.currentCount == 0, "%s: the info count is 0", label);
        PRINTF(info.currentOwnerId == 0, "%s: the info owner is 0", label);
    }
}

int main(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID parked = test_start_thread(parked_entry);
    TEST_WAIT_FOR(test_in_wait(parked, WAIT_TYPE_SEMA, gate));

    PRINT("create the live mutex", sceKernelCreateLwMutex(&live_mutex, "live", 0, 0, NULL));
    LwMutexInfo live_info = { .size = sizeof(live_info) };
    PRINT("info of the live mutex", sceKernelGetLwMutexInfo(&live_mutex, &live_info));
    uid_word[UID_LIVE_MUTEX] = (unsigned int)live_info.uid;

    static SceKernelLwMutexWork deleted;
    PRINT("create the deleted mutex", sceKernelCreateLwMutex(&deleted, "deleted", 0, 0, NULL));
    LwMutexInfo deleted_info = { .size = sizeof(deleted_info) };
    PRINT("info of the deleted mutex", sceKernelGetLwMutexInfo(&deleted, &deleted_info));
    uid_word[UID_DELETED] = (unsigned int)deleted_info.uid;
    PRINT("delete the deleted mutex", sceKernelDeleteLwMutex(&deleted));

    const SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    PRINT("create the semaphore", test_created(sema));
    uid_word[UID_SEMAPHORE] = (unsigned int)sema;
    uid_word[UID_ZERO] = 0;
    uid_word[UID_MINUS_ONE] = 0xFFFFFFFF;
    uid_word[UID_NO_OBJECT] = 0x12345;

    for (int uid = 0; uid < UID_COUNT; uid++) {
        for (int owner = 0; owner < OWNER_COUNT; owner++) {
            for (int call = 0; call < CALL_COUNT; call++)
                run_case(uid, owner, call);
        }
    }

    sceKernelSignalSema(gate, 1);
    test_join_thread(parked);
    sceKernelDeleteLwMutex(&live_mutex);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
