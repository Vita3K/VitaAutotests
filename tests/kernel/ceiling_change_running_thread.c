// What a priority change below a held ceiling does to a thread that runs above that ceiling, by who
// changes it and what the thread does. T, at 0x70 on core 0, locks a mutex with the ceiling 0x80,
// which leaves it at 0x70. T then records its priority at each step, and the main thread, on core 1
// at 0x60, prints T's priority from T's info at its own steps. The cases:
// - main changes T to 0xB0 while T spins, T reads its priority after the spin and after the unlock
// - main changes T to 0xB0 while T waits on a semaphore, T reads its priority once woken and after
//   the unlock
// - T changes its own priority to 0xB0, reads it from its TLS at once, then from its info, then
//   unlocks
// - the same with a delay of 2 ms between the change and the info read
// - the same with a lock and an unlock of a mutex without a ceiling between them
// T records its own priorities in memory and the main thread prints them once T has ended, so no
// print runs on T.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum { SPIN, WAIT, SELF, SELF_DELAY, SELF_PLAIN };

static SceUID ceiling, sema, plain;
static volatile int mode, t_ready, t_go, t_steps[4];

static int own_priority(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    return info.currentPriority;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(ceiling, 1, NULL);
    t_steps[0] = own_priority();
    if (mode == SPIN) {
        t_ready = 1;
        while (!t_go)
            ;
    } else if (mode == WAIT) {
        t_ready = 1;
        SceUInt timeout = 10 * TEST_WAIT_US;
        sceKernelWaitSema(sema, 1, &timeout);
    } else {
        sceKernelChangeThreadPriority(0, 0xB0);
        t_steps[3] = sceKernelGetThreadCurrentPriority();
        if (mode == SELF_DELAY)
            sceKernelDelayThread(2000);
        if (mode == SELF_PLAIN) {
            sceKernelLockMutex(plain, 1, NULL);
            sceKernelUnlockMutex(plain, 1);
        }
        t_ready = 1;
    }
    t_steps[1] = own_priority();
    sceKernelUnlockMutex(ceiling, 1);
    t_steps[2] = own_priority();
    return 0;
}

static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

static void run(const char *name, int m) {
    mode = m;
    t_ready = t_go = 0;
    for (int i = 0; i < 4; i++)
        t_steps[i] = -1;
    SceKernelMutexOptParam opt = { .size = sizeof(opt), .ceilingPriority = 0x80 };
    ceiling = sceKernelCreateMutex("ceiling", SCE_KERNEL_MUTEX_ATTR_CEILING, 0, &opt);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    plain = sceKernelCreateMutex("plain", 0, 0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, 0x70, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(t_ready);
    if (m == WAIT)
        TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, sema));
    if (m == SPIN || m == WAIT) {
        PRINTF(priority_of(t), "%s: T's priority before the change", name);
        PRINTF(sceKernelChangeThreadPriority(t, 0xB0), "%s: the change of T to 0xB0", name);
        PRINTF(priority_of(t), "%s: T's priority after the change", name);
        if (m == SPIN)
            t_go = 1;
        else
            sceKernelSignalSema(sema, 1);
    }
    test_join_thread(t);
    PRINTF(t_steps[0], "%s: T's own priority once it holds the mutex", name);
    if (m != SPIN && m != WAIT)
        PRINTF(t_steps[3], "%s: T's own priority from its TLS right after its change", name);
    static const char *const after[] = { "spin", "wait", "change", "delay", "plain mutex" };
    PRINTF(t_steps[1], "%s: T's own priority in its info after the %s", name, after[m]);
    PRINTF(t_steps[2], "%s: T's own priority after the unlock", name);
    sceKernelDeleteMutex(plain);
    sceKernelDeleteSema(sema);
    sceKernelDeleteMutex(ceiling);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, 0x60);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run("main changes T while it spins", SPIN);
    run("main changes T while it waits", WAIT);
    run("T changes itself", SELF);
    run("T changes itself then delays", SELF_DELAY);
    run("T changes itself then uses a plain mutex", SELF_PLAIN);

    test_exit();
    return 0;
}
