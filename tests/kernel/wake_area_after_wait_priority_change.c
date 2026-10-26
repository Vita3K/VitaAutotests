// Which ready queue a thread joins when it is made ready, and whether it is READY or STANDBY, when
// its priority was changed while it waited. T waits on a semaphore on core 0 at its start priority.
// The main thread, on core 1, then changes T's priority, and U starts on core 0 at the new
// priority. A gate thread of the highest priority holds core 0 until the main thread lifts it.
// T is made ready after that by a signal from core 1, by its own 500 ms timeout, or by the end of
// its 500 ms delay. The change and U's start come well before T's timeout or delay end. Each case
// prints T's and U's status and T's current CPU before the gate lifts, and which of the two runs
// first after it. The controls change no priority.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define SPINS 400000000
#define T_WAIT_US 500000

enum Wake { BY_SIGNAL, BY_TIMEOUT, BY_DELAY, WAKE_COUNT };
static const char *const wake_names[WAKE_COUNT] = { "signal", "timeout", "delay end" };

static SceUID sema, gate;
static volatile int gate_up, gate_open, ran;
static volatile int order[2];

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

static void gate_lift(void) {
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
}

static void record(int who) {
    order[__atomic_fetch_add(&ran, 1, __ATOMIC_SEQ_CST)] = who;
}

// T, which waits for the wake its argument says, then records that it ran
static int t_entry(SceSize args, void *argp) {
    (void)args;
    const int wake = *(const int *)argp;
    SceUInt32 timeout = wake == BY_SIGNAL ? TEST_WAIT_US : T_WAIT_US;
    if (wake == BY_DELAY)
        sceKernelDelayThread(T_WAIT_US);
    else
        sceKernelWaitSema(sema, 1, &timeout);
    record(1);
    return 0;
}

static int u_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    record(2);
    return 0;
}

static SceUID start(SceKernelThreadEntry entry, int priority, int arg) {
    const SceUID thread = sceKernelCreateThread("t", entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(arg), &arg);
    return thread;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// Whether a thread has left its wait and isn't running, so it is READY, STANDBY or has ended
static int settled(SceUID thread) {
    return (info_of(thread).status & (SCE_THREAD_RUNNING | SCE_THREAD_WAITING)) == 0;
}

static void run_case(int wake, int from, int to) {
    ran = 0;
    order[0] = order[1] = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = start(t_entry, from, wake);
    test_wait_until_waiting(t);
    gate_close();
    int change = 0;
    if (from != to)
        change = sceKernelChangeThreadPriority(t, to);
    const SceUID u = start(u_entry, to, 0);
    if (wake == BY_SIGNAL)
        sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(settled(t));
    TEST_WAIT_FOR(settled(u));
    const SceKernelThreadInfo t_info = info_of(t);
    const SceKernelThreadInfo u_info = info_of(u);
    gate_lift();
    TEST_WAIT_FOR(ran == 2);
    // Joined first, so every record the threads made is complete
    test_join_thread(t);
    test_join_thread(u);
    const char *const name = wake_names[wake];
    if (from != to)
        PRINTF(change, "%s, T 0x%X to 0x%X: the priority change", name, from, to);
    PRINTF(t_info.status, "%s, T 0x%X to 0x%X: T's status before the gate lifts", name, from, to);
    PRINTF(t_info.currentCpuId, "%s, T 0x%X to 0x%X: T's current CPU before the gate lifts", name, from, to);
    PRINTF(u_info.status, "%s, T 0x%X to 0x%X: U's status before the gate lifts", name, from, to);
    PRINTF(order[0], "%s, T 0x%X to 0x%X: the first to run, 1 for T and 2 for U", name, from, to);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    static const struct {
        int wake, from, to;
    } cases[] = {
        { BY_SIGNAL, 0x70, 0xA0 }, { BY_SIGNAL, 0xA0, 0x70 }, { BY_SIGNAL, 0xA0, 0x7F },
        { BY_SIGNAL, 0x70, 0x80 }, { BY_SIGNAL, 0xA0, 0x40 }, { BY_SIGNAL, 0x70, 0xBF },
        { BY_TIMEOUT, 0xA0, 0x70 }, { BY_DELAY, 0xA0, 0x70 },
        { BY_SIGNAL, 0x70, 0x70 }, { BY_SIGNAL, 0xA0, 0xA0 },
        { BY_TIMEOUT, 0x70, 0x70 }, { BY_DELAY, 0x70, 0x70 },
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(cases[i].wake, cases[i].from, cases[i].to);
    test_exit();
    return 0;
}
