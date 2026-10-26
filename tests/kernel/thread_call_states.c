// What the thread manager calls return for a thread that is not dormant, in each state: ready
// behind a gate, running on core 0, delaying, suspended for a VM while running, inside its START or
// its END thread event handler, and started again behind a gate after it ended with 0x111, before it
// runs. The main thread runs on core 1 and the test threads on core 0. A gate thread spins on core 0
// at a high priority with no syscalls, so a thread behind it stays ready until the test lifts the
// gate. Each case creates its threads fresh, except the restarted one, which is the same thread.
// For each state the test prints the thread's status and exit status, then makes these calls in
// order and prints each result: StartThread, GetThreadExitStatus, WaitThreadEnd with a 300 ms
// timeout, ChangeThreadPriority, GetThreadInfo, and DeleteThread last. The status variables given to
// the exit status and wait calls are printed too, so a call that writes nothing shows 0xDEAD. If
// DeleteThread fails, the test lets the thread end, prints its exit status, and deletes it. The
// gated threads also print whether they ran before the gate was lifted, read after the calls.
// Cases:
// - ready behind the gate
// - running on core 0, spinning until the test stops it
// - delaying for 3 s
// - suspended for a VM while running
// - in its START handler, which spins before the entry runs
// - in its END handler, after the entry returned
// - started again behind the gate after it ended with 0x111, and the exit status it then reports
// Every spin and handler is bounded in process time, and the gate in its count, so a run that never
// releases a thread still ends.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define T_PRIORITY 0xA0
#define GATE_SPINS 2000000000
#define SPIN_LIMIT_US 10000000
// The timeout of the WaitThreadEnd call made in each state, in microseconds
#define CALL_TIMEOUT 300000

static SceUID gate;
static volatile int gate_up, gate_open;
static volatile int ran, ran_before_lift, stop, handler_in, handler_go;

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < GATE_SPINS && !gate_open; n++)
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
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(gate, NULL, &timeout);
    sceKernelDeleteThread(gate);
}

// Records that it ran, and whether the gate still held it
static void mark_run(void) {
    ran = 1;
    if (!gate_open)
        ran_before_lift = 1;
}

// Marks its run, spins until the test stops it, and returns the status it was started with
static int spin_entry(SceSize args, void *argp) {
    (void)args;
    mark_run();
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    while (!stop && sceKernelGetProcessTimeWide() - start < SPIN_LIMIT_US)
        ;
    return *(const int *)argp;
}

// Records that it ran, sleeps for 3 s, and returns the status it was started with
static int delay_entry(SceSize args, void *argp) {
    (void)args;
    mark_run();
    sceKernelDelayThread(3000000);
    return *(const int *)argp;
}

// Records that it ran, and returns the status it was started with
static int return_entry(SceSize args, void *argp) {
    (void)args;
    mark_run();
    return *(const int *)argp;
}

// The START or END thread event handler of the target. It spins until the test sets handler_go, or
// until the spin limit in process time, and makes no exit or delete call.
static int spin_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    handler_in = 1;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    while (!handler_go && sceKernelGetProcessTimeWide() - start < SPIN_LIMIT_US)
        ;
    return 0;
}

// Creates a test thread on core 0 that can be suspended for a VM
static SceUID create_target(SceKernelThreadEntry entry) {
    return sceKernelCreateThread("t", entry, T_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
}

// Starts t, passing status as the argument its entry returns
static void start_with(SceUID t, int status) {
    sceKernelStartThread(t, sizeof(status), &status);
}

// Returns whether t is in a delay
static int delaying(SceUID t) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(t, &info) == 0 && info.waitType == WAIT_TYPE_DELAY;
}

// Returns whether t has exactly the status
static int has_status(SceUID t, SceUInt32 status) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(t, &info) == 0 && info.status == status;
}

// Prints the info of t before the calls, and returns its exit status
static int print_before(const char *name, SceUID t) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetThreadInfo(t, &info), "%s: info before the calls", name);
    PRINTF(info.status, "%s: the status before the calls", name);
    PRINTF(info.exitStatus, "%s: the exit status before the calls", name);
    return info.exitStatus;
}

// Makes the calls on t, which is not dormant, and prints each result. Returns the result of
// DeleteThread, which is 0 only when it deleted t.
static int print_calls(const char *name, SceUID t) {
    PRINTF(sceKernelStartThread(t, 0, NULL), "%s: start it", name);
    int status = 0xDEAD;
    PRINTF(sceKernelGetThreadExitStatus(t, &status), "%s: the exit status call", name);
    PRINTF(status, "%s: the status the exit status call wrote", name);
    int end_status = 0xDEAD;
    SceUInt timeout = CALL_TIMEOUT;
    PRINTF(sceKernelWaitThreadEnd(t, &end_status, &timeout), "%s: wait for its end", name);
    PRINTF(end_status, "%s: the status the wait wrote", name);
    PRINTF(sceKernelChangeThreadPriority(t, T_PRIORITY), "%s: change its priority", name);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINTF(sceKernelGetThreadInfo(t, &info), "%s: info", name);
    PRINTF(info.status, "%s: its status", name);
    PRINTF(info.exitStatus, "%s: its exit status", name);
    const int deleted = sceKernelDeleteThread(t);
    PRINTF(deleted, "%s: delete it", name);
    return deleted;
}

// Waits for t to end and prints its exit status, then unregisters handler unless it is -1, and
// deletes t.
static void finish(const char *name, SceUID t, SceUID handler) {
    SceUInt timeout = 4 * TEST_WAIT_US;
    int status = 0xDEAD;
    PRINTF(sceKernelWaitThreadEnd(t, &status, &timeout), "%s: wait for its end after the calls", name);
    PRINTF(status, "%s: its exit status after the end", name);
    if (handler >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(handler), "%s: unregister the handler", name);
    PRINTF(sceKernelDeleteThread(t), "%s: delete it after the end", name);
}

static void case_ready(void) {
    const char *name = "ready behind the gate";
    ran = ran_before_lift = stop = 0;
    gate_close();
    const SceUID t = create_target(spin_entry);
    start_with(t, 0x101);
    print_before(name, t);
    const int deleted = print_calls(name, t);
    PRINTF(ran_before_lift, "%s: it ran before the gate lifted", name);
    stop = 1;
    gate_lift();
    if (deleted != 0)
        finish(name, t, -1);
}

static void case_running(void) {
    const char *name = "running on core 0";
    ran = stop = 0;
    const SceUID t = create_target(spin_entry);
    start_with(t, 0x102);
    TEST_WAIT_FOR(ran);
    print_before(name, t);
    const int deleted = print_calls(name, t);
    stop = 1;
    if (deleted != 0)
        finish(name, t, -1);
}

static void case_delaying(void) {
    const char *name = "delaying for 3 s";
    const SceUID t = create_target(delay_entry);
    start_with(t, 0x103);
    TEST_WAIT_FOR(delaying(t));
    print_before(name, t);
    const int deleted = print_calls(name, t);
    if (deleted != 0)
        finish(name, t, -1);
}

// Suspends a spinning thread, so it has run before the suspend call, unlike a ready one
static void case_suspended(void) {
    const char *name = "suspended for a VM while running";
    ran = stop = 0;
    const SceUID t = create_target(spin_entry);
    start_with(t, 0x104);
    TEST_WAIT_FOR(ran);
    PRINTF(sceKernelSuspendThreadForVM(t), "%s: suspend it", name);
    TEST_WAIT_FOR(has_status(t, 0x100));
    print_before(name, t);
    const int deleted = print_calls(name, t);
    stop = 1;
    PRINTF(sceKernelResumeThreadForVM(t), "%s: resume it", name);
    if (deleted != 0)
        finish(name, t, -1);
}

static void case_start_handler(void) {
    const char *name = "in its START handler";
    ran = handler_in = handler_go = 0;
    const SceUID t = create_target(return_entry);
    const SceUID handler =
        sceKernelRegisterThreadEventHandler("start", t, SCE_KERNEL_THREAD_EVENT_TYPE_START, spin_handler, NULL);
    PRINTF(test_created(handler), "%s: register the handler", name);
    start_with(t, 0x105);
    TEST_WAIT_FOR(handler_in);
    PRINTF(ran, "%s: its entry ran", name);
    print_before(name, t);
    const int deleted = print_calls(name, t);
    handler_go = 1;
    if (deleted != 0)
        finish(name, t, handler);
}

static void case_end_handler(void) {
    const char *name = "in its END handler";
    ran = handler_in = handler_go = 0;
    const SceUID t = create_target(return_entry);
    const SceUID handler =
        sceKernelRegisterThreadEventHandler("end", t, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, spin_handler, NULL);
    PRINTF(test_created(handler), "%s: register the handler", name);
    start_with(t, 0x106);
    TEST_WAIT_FOR(handler_in);
    print_before(name, t);
    const int deleted = print_calls(name, t);
    handler_go = 1;
    if (deleted != 0)
        finish(name, t, handler);
}

static void case_restarted(void) {
    const char *name = "started again behind the gate";
    ran = ran_before_lift = stop = 0;
    const SceUID t = create_target(spin_entry);
    start_with(t, 0x111);
    TEST_WAIT_FOR(ran);
    stop = 1;
    int first = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(t, &first, &timeout), "%s: the wait for the first run's end", name);
    PRINTF(first, "%s: the first run's exit status", name);
    ran = ran_before_lift = stop = 0;
    gate_close();
    start_with(t, 0x222);
    const int exit_before = print_before(name, t);
    PRINTF(exit_before == 0x111, "%s: the exit status before the calls equals 0x111", name);
    const int deleted = print_calls(name, t);
    PRINTF(ran_before_lift, "%s: it ran before the gate lifted", name);
    stop = 1;
    gate_lift();
    if (deleted != 0)
        finish(name, t, -1);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    case_ready();
    case_running();
    case_delaying();
    case_suspended();
    case_start_handler();
    case_end_handler();
    case_restarted();

    test_exit();
    return 0;
}
