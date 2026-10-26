// A priority change to a thread in a wait that has no object: a delay, a delay with callbacks, a
// thread signal wait, or a wait for another thread's end. The waiter W and the main thread share
// core 0, and W's wake comes while main spins there, so the wake either takes the core from main
// or waits until main is done. Each kind has two cases:
// - raised: W at 0x80 is raised to 0x50 while it waits, above main at 0x60, so the wake takes the
//   core and the steps are 0 1 2
// - control: W starts at 0x50, is lowered to 0x80 while it waits, and stays below main, so the
//   steps are 0 2 1
// The wake is the delay running out after 400 ms, a signal sent by main, or the end of a target
// thread on core 1, which main lets go. Main records step 0, makes the wake, spins until 800 ms
// after step 0 and records step 2. W records step 1 when its wait returns. Nothing is printed
// until main has recorded step 2. Each case prints the change, W's priority from its info, its
// wait type before and after the change, its result and the steps.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define TARGET_PRIORITY 0x60
#define WAITER_HIGH_PRIORITY 0x50
#define WAITER_LOW_PRIORITY 0x80
#define DELAY_US 400000
#define SPIN_US 800000
#define MAX_STEPS 3

typedef enum Kind { DELAY, DELAY_CB, SIGNAL, END } Kind;

static Kind kind;
static SceUID go, target, waiter;
static volatile int result;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits in the kind the case picks, then records step 1
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    switch (kind) {
    case DELAY: result = sceKernelDelayThread(DELAY_US); break;
    case DELAY_CB: result = sceKernelDelayThreadCB(DELAY_US); break;
    case SIGNAL: result = sceKernelWaitSignal(0, 0, &timeout); break;
    case END: result = sceKernelWaitThreadEnd(target, NULL, &timeout); break;
    }
    record(1);
    return 0;
}

// The target of the end wait, which ends once main lets go of the semaphore
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return 0;
}

static int wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

static int priority_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

// Spins on core 0 until the process time reaches end, without sleeping
static void spin_until(SceUInt64 end) {
    while (sceKernelGetProcessTimeWide() < end)
        ;
}

// Starts W at start_priority, changes it to new_priority while it waits, then makes the wake
static void run_case(const char *name, Kind k, int start_priority, int new_priority) {
    kind = k;
    result = 0xDEAD;
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    if (k == END) {
        go = sceKernelCreateSema("go", 0, 0, 1, NULL);
        target = sceKernelCreateThread("target", target_entry, TARGET_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
        sceKernelStartThread(target, 0, NULL);
    }
    waiter = sceKernelCreateThread("w", waiter_entry, start_priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter, 0, NULL);
    test_wait_until_waiting(waiter);

    const int before = wait_type_of(waiter);
    const int change = sceKernelChangeThreadPriority(waiter, new_priority);
    const int priority = priority_of(waiter);
    const int after = wait_type_of(waiter);

    const SceUInt64 start = sceKernelGetProcessTimeWide();
    record(0);
    int wake = 0;
    if (k == SIGNAL)
        wake = sceKernelSendSignal(waiter);
    if (k == END)
        wake = sceKernelSignalSema(go, 1);
    spin_until(start + SPIN_US);
    record(2);

    test_join_thread(waiter);
    if (k == END) {
        test_join_thread(target);
        sceKernelDeleteSema(go);
    }
    if (k == SIGNAL || k == END)
        PRINTF(wake, "%s: the wake", name);
    PRINTF(change, "%s: the change", name);
    PRINTF(priority, "%s: W's priority from its info", name);
    PRINTF(before, "%s: W's wait type before the change", name);
    PRINTF(after, "%s: W's wait type after the change", name);
    PRINTF(result, "%s: W's result", name);
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("delay raised from 0x80 to 0x50", DELAY, WAITER_LOW_PRIORITY, WAITER_HIGH_PRIORITY);
    run_case("delay started at 0x50 and lowered to 0x80", DELAY, WAITER_HIGH_PRIORITY, WAITER_LOW_PRIORITY);
    run_case("delay with callbacks raised from 0x80 to 0x50", DELAY_CB, WAITER_LOW_PRIORITY,
             WAITER_HIGH_PRIORITY);
    run_case("delay with callbacks started at 0x50 and lowered to 0x80", DELAY_CB, WAITER_HIGH_PRIORITY,
             WAITER_LOW_PRIORITY);
    run_case("thread signal wait raised from 0x80 to 0x50", SIGNAL, WAITER_LOW_PRIORITY, WAITER_HIGH_PRIORITY);
    run_case("thread signal wait started at 0x50 and lowered to 0x80", SIGNAL, WAITER_HIGH_PRIORITY,
             WAITER_LOW_PRIORITY);
    run_case("wait for a thread's end raised from 0x80 to 0x50", END, WAITER_LOW_PRIORITY, WAITER_HIGH_PRIORITY);
    run_case("wait for a thread's end started at 0x50 and lowered to 0x80", END, WAITER_HIGH_PRIORITY,
             WAITER_LOW_PRIORITY);

    test_exit();
    return 0;
}
