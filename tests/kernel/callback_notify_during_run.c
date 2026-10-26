// What the owner of a callback and the callback itself see, and what notifies do, while the
// callback runs. The owner T runs on core 1. It notifies its callback A and runs a check. A's first
// run waits on a gate semaphore with a timeout, so T stays inside A until the main thread, on core
// 0, opens the gate. While A waits, the main thread reads A's count, argument and notify ID and T's
// thread info, then notifies A twice, with 7 and then 8, and notifies a second callback B of T
// once. It opens the gate and joins T. After the gate, A reads its own info inside its first run,
// and T then runs a second check. The main thread prints after T ended. Each case has a fresh gate,
// fresh callbacks and a fresh T. The cases differ in the value A's first run returns:
// - 0
// - 1
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// How long A's first run waits on the gate, long enough for the main thread to make all its calls
#define GATE_TIMEOUT_US 6000000

// The value that A's first run returns, set by the case
static int a_return;
static SceUID gate, cb_a, cb_b;
static volatile int a_runs, b_runs, a_in_gate, a_gate_result;
static volatile int run_count[3], run_arg[3];
static volatile int a_after_count, a_after_arg, a_after_id;
static volatile int first_check, b_after_first, second_check;

// Counts a run of A and keeps the count and argument of each run. The first run waits on the gate,
// reads its own info after it, and returns a_return.
static int a_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    a_runs++;
    run_count[a_runs] = count;
    run_arg[a_runs] = arg;
    if (a_runs > 1)
        return 0;
    a_in_gate = 1;
    SceUInt timeout = GATE_TIMEOUT_US;
    a_gate_result = sceKernelWaitSema(gate, 1, &timeout);
    CallbackInfo info = { .size = sizeof(info) };
    sceKernelGetCallbackInfo(cb_a, (SceKernelCallbackInfo *)&info);
    a_after_count = info.notifyCount;
    a_after_arg = info.notifyArg;
    a_after_id = info.notifyId;
    return a_return;
}

// Counts the runs of B
static int b_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    b_runs++;
    return 0;
}

// Creates A and B, notifies A, then runs two checks and records what each one returns
static int owner_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb_a = sceKernelCreateCallback("a", 0, a_callback, NULL);
    cb_b = sceKernelCreateCallback("b", 0, b_callback, NULL);
    sceKernelNotifyCallback(cb_a, 1);
    first_check = sceKernelCheckCallback();
    b_after_first = b_runs;
    second_check = sceKernelCheckCallback();
    return 0;
}

// Runs the owner with A returning return_value from its first run, and prints what the main thread
// sees while A waits, then what the owner records after it ended
static void run_case(int return_value, const char *name) {
    a_return = return_value;
    a_runs = 0;
    b_runs = 0;
    a_in_gate = 0;
    a_gate_result = 0;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID owner = sceKernelCreateThread("owner", owner_thread, SCE_KERNEL_DEFAULT_PRIORITY_USER,
                                               0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(a_in_gate);

    CallbackInfo info = { .size = sizeof(info) };
    sceKernelGetCallbackInfo(cb_a, (SceKernelCallbackInfo *)&info);
    PRINTF(info.notifyCount, "%s: A's count before the notifies", name);
    PRINTF(info.notifyArg, "%s: A's argument before the notifies", name);
    PRINTF(info.notifyId, "%s: A's notify ID before the notifies", name);

    PRINTF(sceKernelNotifyCallback(cb_a, 7), "%s: notify A with 7", name);
    PRINTF(sceKernelNotifyCallback(cb_a, 8), "%s: notify A with 8", name);
    PRINTF(sceKernelNotifyCallback(cb_b, 0x55), "%s: notify B once", name);
    sceKernelGetCallbackInfo(cb_a, (SceKernelCallbackInfo *)&info);
    PRINTF(info.notifyCount, "%s: A's count after the notifies", name);
    PRINTF(info.notifyArg, "%s: A's argument after the notifies", name);
    PRINTF(info.notifyId == -1, "%s: A's notify ID after the notifies is -1", name);
    PRINTF(sceKernelGetCallbackCount(cb_a), "%s: A's count after the notifies, from the count call", name);
    PRINTF(sceKernelGetCallbackCount(cb_b), "%s: B's count after its notify", name);
    PRINTF(b_runs, "%s: B's runs during A's run", name);
    PRINTF(a_runs, "%s: A's runs during A's run", name);

    SceKernelThreadInfo thread_info = { .size = sizeof(thread_info) };
    sceKernelGetThreadInfo(owner, &thread_info);
    PRINTF(thread_info.fNotifyCallback, "%s: the owner's notify flag during the run", name);
    PRINTF(thread_info.waitType, "%s: the owner's wait type during the run", name);
    PRINTF(thread_info.waitId == gate, "%s: the owner's wait is on the gate during the run", name);

    PRINTF(sceKernelSignalSema(gate, 1), "%s: open the gate", name);
    test_join_thread(owner);
    PRINTF(a_gate_result, "%s: A's gate wait", name);
    PRINTF(a_after_count, "%s: A's count in its info after the gate", name);
    PRINTF(a_after_arg, "%s: A's argument in its info after the gate", name);
    PRINTF(a_after_id == -1, "%s: A's notify ID in its info after the gate is -1", name);
    PRINTF(run_count[1], "%s: the count A's first run got", name);
    PRINTF(run_arg[1], "%s: the argument A's first run got", name);
    PRINTF(first_check, "%s: the first check's result", name);
    PRINTF(b_after_first, "%s: B's runs after the first check", name);
    PRINTF(second_check, "%s: the second check's result", name);
    PRINTF(run_count[2], "%s: the count A's second run got", name);
    PRINTF(run_arg[2], "%s: the argument A's second run got", name);
    PRINTF(a_runs, "%s: A's runs in total", name);
    PRINTF(b_runs, "%s: B's runs in total", name);
    sceKernelDeleteSema(gate);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    run_case(0, "returns 0");
    run_case(1, "returns 1");

    test_exit();
    return 0;
}
