// The count of a lock, a try lock and an unlock on a mutex is checked before the state of the mutex.
// Each call runs on a fresh mutex in one of three states: free, owned by the calling thread with a
// count of 1, or owned by another thread, which holds it until the test releases it. The counts
// that a mutex can't take are 0, -1, 2, 0x7FFFFFFF and 0x80000000 on a plain mutex, and 0, -1 and
// 0x80000000 on a recursive one, so each of those calls should fail on its count in every state. A
// count of 1 on a plain mutex is the control, which shows that the states are set up as meant. Each
// lock passes a timeout variable of 0, so a lock that had to wait would fail at once. After each
// call the test prints its result, the count and the owner, and whether a lock left its timeout
// variable as it was. The last cases pass a timeout of 500 ms with a count that can't be used,
// which should fail at once and leave the variable, and the owner of a recursive mutex unlocks it
// by 5 against an unlock of 5 by another thread, which holds it once.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define TIMEOUT_500_MS 500000

enum { FREE, OWNED_BY_CALLER, OWNED_BY_OTHER };
enum { LOCK_CALL, TRY_CALL, UNLOCK_CALL };

static const char *const state_names[] = { "free", "owned by the caller", "owned by another thread" };
static const char *const call_names[] = { "lock", "try lock", "unlock" };

static SceUID mutex, holder, release;
static volatile int held_ready, holder_unlock_five, holder_five_result;

// Holds the mutex until release is signaled, then, if the case asks, unlocks it by 5 before its own
// unlock of 1
static int holder_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockMutex(mutex, 1, &timeout);
    held_ready = 1;
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    if (holder_unlock_five)
        holder_five_result = sceKernelUnlockMutex(mutex, 5);
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Maps the owner to 0 for none, 1 for the calling thread, 2 for the holder and 3 for anything else
static int owner_code(SceUID owner) {
    if (owner == 0)
        return 0;
    if (owner == sceKernelGetThreadId())
        return 1;
    if (owner == holder)
        return 2;
    return 3;
}

// Makes one call on a fresh mutex with the attributes and the count, in the state, with the timeout
// for a lock, and prints the result, the count and the owner after it, and for a lock whether its
// timeout variable is unchanged
static void call_case(const char *label, SceUInt attr, int count, int state, int call, SceUInt time) {
    mutex = sceKernelCreateMutex("mutex", attr, 0, NULL);
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    held_ready = 0;
    holder = -1;
    if (state == OWNED_BY_CALLER) {
        SceUInt timeout = TEST_WAIT_US;
        sceKernelLockMutex(mutex, 1, &timeout);
    } else if (state == OWNED_BY_OTHER) {
        holder = test_start_thread(holder_thread);
        TEST_WAIT_FOR(held_ready);
    }

    SceUInt timeout = time;
    int result = 0;
    if (call == LOCK_CALL)
        result = sceKernelLockMutex(mutex, count, &timeout);
    else if (call == TRY_CALL)
        result = sceKernelTryLockMutex(mutex, count);
    else
        result = sceKernelUnlockMutex(mutex, count);

    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    PRINTF(result, "%s, %s: the result", label, call_names[call]);
    PRINTF(info.currentCount, "%s, %s: the count after", label, call_names[call]);
    PRINTF(owner_code(info.currentOwnerId), "%s, %s: the owner after", label, call_names[call]);
    if (call == LOCK_CALL)
        PRINTF(timeout == time, "%s, %s: the timeout is unchanged", label, call_names[call]);

    if (holder != -1) {
        sceKernelSignalSema(release, 1);
        test_join_thread(holder);
    }
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(release);
}

// The owner of a recursive mutex holds it once, and another thread unlocks it by 5. Then the owner
// unlocks it by 5 too.
static void owner_unlock_case(void) {
    mutex = sceKernelCreateMutex("mutex", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    held_ready = 0;
    holder_five_result = 0xDEAD;
    holder_unlock_five = 1;
    holder = test_start_thread(holder_thread);
    TEST_WAIT_FOR(held_ready);
    PRINT("recursive, owned by another thread: the unlock of 5 by a thread that doesn't own it",
          sceKernelUnlockMutex(mutex, 5));
    sceKernelSignalSema(release, 1);
    test_join_thread(holder);
    PRINT("recursive, owned by another thread: the owner's unlock of 5 with 1 held", holder_five_result);
    holder_unlock_five = 0;
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(release);
}

int main(void) {
    static const struct {
        const char *name;
        SceUInt attr;
        int count;
    } rows[] = {
        { "control, plain, count 1", 0, 1 },
        { "plain, count 0", 0, 0 },
        { "plain, count -1", 0, -1 },
        { "plain, count 2", 0, 2 },
        { "plain, count 0x7FFFFFFF", 0, 0x7FFFFFFF },
        { "plain, count 0x80000000", 0, -2147483647 - 1 },
        { "recursive, count 0", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0 },
        { "recursive, count -1", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, -1 },
        { "recursive, count 0x80000000", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, -2147483647 - 1 },
    };
    for (unsigned int r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        for (int state = FREE; state <= OWNED_BY_OTHER; state++) {
            for (int call = LOCK_CALL; call <= UNLOCK_CALL; call++) {
                char label[96];
                sceClibSnprintf(label, sizeof(label), "%s, %s", rows[r].name, state_names[state]);
                call_case(label, rows[r].attr, rows[r].count, state, call, 0);
            }
        }
    }

    // The timeout must be unchanged after a lock that fails on its count while another thread owns
    // the mutex, and while the caller owns it, so a wait for the lock would show as a changed timeout
    call_case("plain, count 2, owned by another thread, timeout 500 ms", 0, 2, OWNED_BY_OTHER, LOCK_CALL,
              TIMEOUT_500_MS);
    call_case("recursive, count 0, owned by the caller, timeout 500 ms", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0,
              OWNED_BY_CALLER, LOCK_CALL, TIMEOUT_500_MS);
    call_case("plain, count 0x7FFFFFFF, free, timeout 500 ms", 0, 0x7FFFFFFF, FREE, LOCK_CALL, TIMEOUT_500_MS);

    owner_unlock_case();

    test_exit();
    return 0;
}
