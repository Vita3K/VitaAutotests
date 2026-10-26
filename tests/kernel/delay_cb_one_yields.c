// Whether a delay of 1 microsecond with callbacks gives up the core to a thread of the same
// priority that is ready behind the caller, like a plain delay does. Both threads are pinned to
// core 0 at the same priority, and the main thread on core 1 starts the waiter O first, waits until
// it is waiting, then starts the caller C. C records 0 and signals the semaphore O waits on, which
// makes O ready behind C. C then spins without syscalls until O has run, or for a fixed number of
// loops, so a woken thread which runs before its waker's next line has recorded its step before the
// delay. C keeps the core while it spins, so on a Vita O can't run during the spin, and the delay
// is made in the same state as without it. After the delay C records 2. O records 1 when it runs.
// The steps should be 0 1 2, since the delay gives up the core to O. Cases:
// - a delay with callbacks, sceKernelDelayThreadCB(1), at 0x70 and at 0xA0
// - a plain delay, sceKernelDelayThread(1), at 0x70 and at 0xA0, as controls
//
// Each case prints the result of the delay and the steps in the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_STEPS 4
// A few seconds on a Vita at most, the spin normally ends once O ran
#define SPINS 100000000

typedef enum Call { DELAY_CB, DELAY } Call;

static SceUID go_sema, other, caller;
static volatile int delay_result, other_ran;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits on the semaphore, then records 1
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    record(1);
    other_ran = 1;
    return 0;
}

// Records 0 and signals the semaphore, so O is ready behind it. Spins until O ran, then delays and
// records 2
static int caller_entry(SceSize args, void *argp) {
    (void)args;
    const Call call = *(const Call *)argp;
    record(0);
    sceKernelSignalSema(go_sema, 1);
    for (volatile int n = 0; n < SPINS && !other_ran; n++)
        ;
    delay_result = call == DELAY_CB ? sceKernelDelayThreadCB(1) : sceKernelDelayThread(1);
    record(2);
    return 0;
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

static void run_case(const char *name, int priority, Call call) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    delay_result = 0xDEAD;
    other_ran = 0;
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);

    other = sceKernelCreateThread("other", other_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(other, 0, NULL);
    test_wait_until_waiting(other);
    caller = sceKernelCreateThread("caller", caller_entry, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(caller, sizeof(call), &call);

    // Joined first, so every record the threads made is complete
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(caller, NULL, &timeout);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(other, NULL, &timeout);
    sceKernelDeleteThread(caller);
    sceKernelDeleteThread(other);
    sceKernelDeleteSema(go_sema);

    PRINTF(delay_result, "%s: the delay's result", name);
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("a delay of 1 with callbacks, priority 0x70", 0x70, DELAY_CB);
    run_case("a delay of 1 with callbacks, priority 0xA0", 0xA0, DELAY_CB);
    run_case("a plain delay of 1, priority 0x70", 0x70, DELAY);
    run_case("a plain delay of 1, priority 0xA0", 0xA0, DELAY);

    test_exit();
    return 0;
}
