// What sceKernelChangeThreadPriority2 returns and does for a thread that holds a mutex with a
// ceiling. Owner O, on core 0, locks a mutex with the ceiling 0x80 and waits on a semaphore, while
// the main thread, on core 1 at 0xA0, changes O's priority and reads O's current priority from its
// info after each call. After the main thread signals the semaphore, O unlocks the mutex and
// notes its priority. The cases:
// - O at 0xB0, changed to 0xB8 and then to 0xBC, both below the ceiling
// - O at 0xB0, changed to 0x80, exactly the ceiling
// - O at 0x70, above the ceiling, changed to 0x78 between its priority and the ceiling, then to
//   0x84 and 0xB0 below the ceiling
// - the main thread holds such a mutex itself, changes its own priority to 0xB0 and then to 0
// - the main thread holds such a mutex and changes a thread W at 0xB0, which waits on a semaphore
//   and holds no mutex, to priority 0
// For each call the test prints what it returned and the priority afterwards.
//
// On a PS Vita TV on firmware 3.65 O runs at the ceiling 0x80 while it holds the
// mutex. The call returns the priority the thread runs at, not the one stored last. A change to
// 0xB8 and then to 0xBC returns 0x80 both times and leaves O at 0x80, and O gets 0xBC at the
// unlock. A change to 0x80 returns 0x80 and leaves 0x80. For O at 0x70, a change to 0x78 is
// applied at once and returns 0x70. The changes to 0x84 and 0xB0 return 0x78 and leave O at 0x78,
// as O doesn't fall to the ceiling, and O gets 0xB0 at the unlock. The main thread running at 0x80
// with its own mutex gets 0x80 back from a change to 0xB0 and stays at 0x80, and 0 stands for its
// 0x80, so the base is 0x80 at the unlock. Priority 0 given for W stands for the caller's 0x80,
// returns W's 0xB0 and moves W to 0x80.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CEILING 0x80
#define MAX_CALLS 4

int sceKernelChangeThreadPriority2(SceUID thid, int priority);

static SceUID mutex, sema;
static volatile int t_ready, after_unlock;

static SceUID create_ceiling(void) {
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    return sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
}

static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static int owner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    t_ready = 1;
    SceUInt32 timeout = 10 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    sceKernelUnlockMutex(mutex, 1);
    after_unlock = sceKernelGetThreadCurrentPriority();
    return 0;
}

static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 10 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

static void start_owner(SceUID *thread, int priority) {
    t_ready = after_unlock = 0;
    mutex = create_ceiling();
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    *thread = sceKernelCreateThread("o", owner_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(*thread, 0, NULL);
    TEST_WAIT_FOR(t_ready);
    TEST_WAIT_FOR(test_in_wait(*thread, WAIT_TYPE_SEMA, sema));
}

// Runs one case on an owner at the given priority, changed to each of the priorities in turn
static void run_owner(const char *name, int owner_priority, int count, const int *priorities) {
    int returns[MAX_CALLS], currents[MAX_CALLS];
    SceUID o;
    start_owner(&o, owner_priority);
    for (int i = 0; i < count; i++) {
        returns[i] = sceKernelChangeThreadPriority2(o, priorities[i]);
        currents[i] = priority_of(o);
    }
    sceKernelSignalSema(sema, 1);
    test_join_thread(o);
    for (int i = 0; i < count; i++) {
        PRINTF(returns[i], "%s: the change to 0x%X returns", name, priorities[i]);
        PRINTF(currents[i], "%s: O's priority after the change to 0x%X", name, priorities[i]);
    }
    PRINTF(after_unlock, "%s: O's priority after its unlock", name);
    sceKernelDeleteSema(sema);
    sceKernelDeleteMutex(mutex);
}

static void run_main_own(void) {
    mutex = create_ceiling();
    sceKernelLockMutex(mutex, 1, NULL);
    const int boosted = sceKernelGetThreadCurrentPriority();
    const int first = sceKernelChangeThreadPriority2(0, 0xB0);
    const int first_current = sceKernelGetThreadCurrentPriority();
    const int second = sceKernelChangeThreadPriority2(0, 0);
    const int second_current = sceKernelGetThreadCurrentPriority();
    const int unlock = sceKernelUnlockMutex(mutex, 1);
    const int after = sceKernelGetThreadCurrentPriority();
    sceKernelChangeThreadPriority(0, 0xA0);
    PRINTF(boosted, "main holds the mutex: its priority");
    PRINTF(first, "main holds the mutex: the change of itself to 0xB0 returns");
    PRINTF(first_current, "main holds the mutex: its priority after the change to 0xB0");
    PRINTF(second, "main holds the mutex: the change of itself to 0 returns");
    PRINTF(second_current, "main holds the mutex: its priority after the change to 0");
    PRINTF(unlock, "main holds the mutex: the unlock returns");
    PRINTF(after, "main holds the mutex: its priority after the unlock");
    sceKernelDeleteMutex(mutex);
}

static void run_main_other(void) {
    mutex = create_ceiling();
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID w = sceKernelCreateThread("w", waiter_entry, 0xB0, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_SEMA, sema));
    sceKernelLockMutex(mutex, 1, NULL);
    const int returned = sceKernelChangeThreadPriority2(w, 0);
    const int current = priority_of(w);
    sceKernelUnlockMutex(mutex, 1);
    sceKernelSignalSema(sema, 1);
    test_join_thread(w);
    PRINTF(returned, "main holds the mutex: the change of W to 0 returns");
    PRINTF(current, "main holds the mutex: W's priority after the change to 0");
    sceKernelDeleteSema(sema);
    sceKernelDeleteMutex(mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, 0xA0);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    static const int below[] = { 0xB8, 0xBC };
    static const int exact[] = { 0x80 };
    static const int above[] = { 0x78, 0x84, 0xB0 };
    run_owner("O at 0xB0", 0xB0, 2, below);
    run_owner("O at 0xB0 and the exact ceiling", 0xB0, 1, exact);
    run_owner("O at 0x70", 0x70, 3, above);
    run_main_own();
    run_main_other();

    test_exit();
    return 0;
}
