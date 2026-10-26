// Whether a wait or poll that fails at once gives up the core to a thread of the same priority that
// is ready behind the caller. The thread O at 0x70 waits on a semaphore, and the thread L at 0x70
// runs on core 0 too. The main thread, on core 1, starts O, waits until it is waiting, starts L,
// and joins both. L records 0 and signals O, so O is ready behind L. L then spins without syscalls
// until O has run, or for a fixed number of loops. The spin only makes sure that a woken thread
// which runs before its waker's next line has recorded its step before the call. L keeps the core
// while it spins, so on a Vita O can't run during the spin, and the call is made in the same state
// as without it. After the spin L makes a call that fails at once, records 2, and waits for O to
// end. O records 1 when it runs and signals L. The steps should be 0 2 1, since the failing call
// doesn't yield and O only runs once L waits. Cases:
// - sceKernelWaitSema on an empty semaphore with a timeout of 0
// - sceKernelPollSema on an empty semaphore
// - sceKernelPollEventFlag for a bit that isn't set
//
// Each case prints the result of the call and the steps in the order they were recorded.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define PRIORITY 0x70
#define MAX_STEPS 4
// A few seconds on a Vita at most, the spin normally ends once O ran
#define SPINS 100000000

typedef enum Call { WAIT_SEMA, POLL_SEMA, POLL_FLAG } Call;

static SceUID go_sema, done_sema, free_sema, flag, other, low;
static volatile int call_result, other_ran;
static int step_count, steps[MAX_STEPS];

static void record(int step) {
    const int i = __atomic_fetch_add(&step_count, 1, __ATOMIC_SEQ_CST);
    if (i < MAX_STEPS)
        steps[i] = step;
}

// Waits until L signals it, records 1, then signals L
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    record(1);
    other_ran = 1;
    sceKernelSignalSema(done_sema, 1);
    return 0;
}

// Records 0, readies O, spins until O ran, makes the call, records 2, then waits for O to finish
static int low_entry(SceSize args, void *argp) {
    (void)args;
    const Call call = *(const Call *)argp;
    record(0);
    sceKernelSignalSema(go_sema, 1);
    for (volatile int n = 0; n < SPINS && !other_ran; n++)
        ;
    SceUInt timeout = 0;
    SceUInt32 bits = 0;
    switch (call) {
    case WAIT_SEMA:
        call_result = sceKernelWaitSema(free_sema, 1, &timeout);
        break;
    case POLL_SEMA:
        call_result = sceKernelPollSema(free_sema, 1);
        break;
    case POLL_FLAG:
        call_result = sceKernelPollEventFlag(flag, 0x1, SCE_EVENT_WAITOR, &bits);
        break;
    }
    record(2);
    timeout = TEST_WAIT_US;
    sceKernelWaitSema(done_sema, 1, &timeout);
    return 0;
}

static void print_steps(const char *name) {
    const int count = step_count < MAX_STEPS ? step_count : MAX_STEPS;
    PRINTF(step_count, "%s: steps recorded", name);
    for (int i = 0; i < count; i++)
        PRINTF(steps[i], "%s: step %d", name, i);
}

static void run_case(const char *name, Call call) {
    step_count = 0;
    for (int i = 0; i < MAX_STEPS; i++)
        steps[i] = -1;
    call_result = 0xDEAD;
    other_ran = 0;
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);
    done_sema = sceKernelCreateSema("done", 0, 0, 1, NULL);
    free_sema = sceKernelCreateSema("free", 0, 0, 1, NULL);
    flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);

    other = sceKernelCreateThread("other", other_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(other, 0, NULL);
    test_wait_until_waiting(other);
    low = sceKernelCreateThread("low", low_entry, PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(low, sizeof(call), &call);

    // Joined first, so every record the threads made is complete
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(low, NULL, &timeout);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(other, NULL, &timeout);
    sceKernelDeleteThread(low);
    sceKernelDeleteThread(other);
    sceKernelDeleteSema(go_sema);
    sceKernelDeleteSema(done_sema);
    sceKernelDeleteSema(free_sema);
    sceKernelDeleteEventFlag(flag);

    PRINTF(call_result, "%s: the call", name);
    print_steps(name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("sceKernelWaitSema with a timeout of 0 on an empty semaphore", WAIT_SEMA);
    run_case("sceKernelPollSema on an empty semaphore", POLL_SEMA);
    run_case("sceKernelPollEventFlag for an unset bit", POLL_FLAG);

    test_exit();
    return 0;
}
