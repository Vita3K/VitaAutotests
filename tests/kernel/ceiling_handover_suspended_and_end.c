// What a thread has for its priority when a ceiling mutex is handed to it while it is suspended,
// and what an owner that ends has for its own priority. The ceiling is 0x80 in every case. The main
// thread runs on core 1 at 0xA0. W is created with the VM attribute on core 0 and the main thread
// holds it with sceKernelSuspendThreadForVM while it waits for the mutex, so W can't run after the
// handover. The main thread polls until W settles, then reads W's status, its current and initial
// priorities from its info, and who owns the mutex, all while W is suspended. Cases:
// - (a) main locks the mutex, W at 0xB8 waits for it, main suspends W and unlocks. It prints W's
//   values, its own priority after the unlock, resumes W and prints W's priority after W's unlock.
// - (b) the same with W at 0x60, which is above the ceiling.
// - (c) O at 0xB0 on core 0 locks the mutex, sets itself to 0xB8, waits on a semaphore and returns
//   without unlocking. W at 0x90 waits for the mutex and is suspended. The main thread signals O
//   and waits for its end, then prints O's current priority from its info, W's values once it
//   settles, the owner and the count.
// - (d) O at 0xB0 locks the mutex with no waiter and returns. The main thread prints O's current
//   priority after its end and the mutex count. Then O is started again with an entry that sets
//   itself to 0xB8 and reads its current priority at once, which is printed.
//
// On a PS Vita TV on firmware 3.65 W has the status 0x100 once it settles. In (a) W's
// current priority is 0x80 with initial priority 0xB8, W owns the mutex and the main thread is at
// 0xA0 again after its unlock. After its own unlock W is at 0xB8. In (b) W's current priority is
// 0x60, and 0x60 after its unlock. In (c) O's current priority after its end is 0xB8, its base,
// W's current priority is 0x80, W owns the mutex and the count is 1. In (d) O's current priority
// after its end is 0xB0 and the count is 0. The restarted O reads 0xB8 at once.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0xA0
#define CEILING 0x80
#define NOT_SET 0xDEAD

static SceUID mutex, go;
static int o_mode;
static volatile int w_lock, w_unlock, w_after, o_read;

// Locks the mutex, waits for go and ends without giving it back. In mode 2 it only reads.
static int o_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (o_mode == 2) {
        sceKernelChangeThreadPriority(0, 0xB8);
        o_read = sceKernelGetThreadCurrentPriority();
        return 0;
    }
    sceKernelLockMutex(mutex, 1, NULL);
    if (o_mode == 1) {
        sceKernelChangeThreadPriority(0, 0xB8);
        SceUInt32 timeout = 2 * TEST_WAIT_US;
        sceKernelWaitSema(go, 1, &timeout);
    }
    return 0;
}

// Locks the mutex, unlocks it and notes its priority
static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    w_lock = sceKernelLockMutex(mutex, 1, &timeout);
    w_unlock = sceKernelUnlockMutex(mutex, 1);
    w_after = sceKernelGetThreadCurrentPriority();
    return 0;
}

static int w_settled(SceUID w) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(w, &info) == 0 && info.waitType == 0 &&
        !(info.status & (SCE_THREAD_RUNNING | SCE_THREAD_READY));
}

static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static void create_mutex(void) {
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = CEILING };
    mutex = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    w_lock = w_unlock = w_after = o_read = NOT_SET;
}

static SceUID start_suspended_waiter(int priority) {
    const SceUID w = sceKernelCreateThread("w", w_entry, priority, 0x4000, THREAD_ATTR_VM,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_MUTEX, mutex));
    sceKernelSuspendThreadForVM(w);
    return w;
}

// Prints what W and the mutex show while W is suspended after the handover
static void print_w(const char *name, SceUID w) {
    TEST_WAIT_FOR(w_settled(w));
    SceKernelThreadInfo thread_info = { .size = sizeof(thread_info) };
    sceKernelGetThreadInfo(w, &thread_info);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int owns = info.currentOwnerId == w;
    const int count = info.currentCount;
    PRINTF(thread_info.status, "%s: W's status once it settles", name);
    PRINTF(thread_info.currentPriority, "%s: W's current priority", name);
    PRINTF(thread_info.initPriority, "%s: W's initial priority", name);
    PRINTF(owns, "%s: W owns the mutex", name);
    PRINTF(count, "%s: the count", name);
}

static void main_unlocks_case(const char *name, int w_priority) {
    create_mutex();
    sceKernelLockMutex(mutex, 1, NULL);
    const SceUID w = start_suspended_waiter(w_priority);
    sceKernelUnlockMutex(mutex, 1);
    const int main_after = sceKernelGetThreadCurrentPriority();
    print_w(name, w);
    PRINTF(main_after, "%s: the main thread's priority after its unlock", name);
    sceKernelResumeThreadForVM(w);
    test_join_thread(w);
    PRINTF(w_lock, "%s: W's lock", name);
    PRINTF(w_unlock, "%s: W's unlock", name);
    PRINTF(w_after, "%s: W's priority after its unlock", name);
    sceKernelDeleteSema(go);
    sceKernelDeleteMutex(mutex);
}

static void owner_ends_case(void) {
    const char *name = "owner ends with a waiter";
    create_mutex();
    o_mode = 1;
    const SceUID o = sceKernelCreateThread("o", o_entry, 0xB0, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(o, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(o, WAIT_TYPE_SEMA, go));
    const SceUID w = start_suspended_waiter(0x90);
    sceKernelSignalSema(go, 1);
    sceKernelWaitThreadEnd(o, NULL, NULL);
    const int o_current = priority_of(o);
    sceKernelDeleteThread(o);
    PRINTF(o_current, "%s: O's current priority after its end", name);
    print_w(name, w);
    sceKernelResumeThreadForVM(w);
    test_join_thread(w);
    PRINTF(w_lock, "%s: W's lock", name);
    PRINTF(w_after, "%s: W's priority after its unlock", name);
    sceKernelDeleteSema(go);
    sceKernelDeleteMutex(mutex);
}

static void owner_ends_alone_case(void) {
    const char *name = "owner ends alone";
    create_mutex();
    o_mode = 0;
    const SceUID o = sceKernelCreateThread("o", o_entry, 0xB0, 0x4000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(o, 0, NULL);
    sceKernelWaitThreadEnd(o, NULL, NULL);
    const int o_current = priority_of(o);
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    const int count = info.currentCount;
    PRINTF(o_current, "%s: O's current priority after its end", name);
    PRINTF(count, "%s: the count", name);
    o_mode = 2;
    sceKernelStartThread(o, 0, NULL);
    sceKernelWaitThreadEnd(o, NULL, NULL);
    sceKernelDeleteThread(o);
    PRINTF(o_read, "%s: O's priority when it runs again", name);
    sceKernelDeleteSema(go);
    sceKernelDeleteMutex(mutex);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    main_unlocks_case("main unlocks, W below the ceiling", 0xB8);
    main_unlocks_case("main unlocks, W above the ceiling", 0x60);
    owner_ends_case();
    owner_ends_alone_case();

    test_exit();
    return 0;
}
