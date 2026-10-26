// What a thread end wait gets when its timeout, the end of its target or a callback races with it.
// Waiter W runs on core 0 below a gate thread that spins there without syscalls, so once W's wait
// ends it stays ready until the gate lets go. Target T runs on core 1, so it can end while the gate
// holds core 0, and the main thread runs on core 2. Cases:
// - timeout then end: W waits 300 ms for T's end, with its status variable preset to 0xDEAD. T is
//   held until 700 ms, then ends with 0x5A while the gate holds W. Prints W's status, wait type and
//   wait UID once the timeout passed, then W's result, whether its status is still 0xDEAD and
//   whether its time left is 0.
// - end then delay: W waits 1500 ms, T ends at once, and the gate holds W for 2 s after that.
//   Prints W's result, the status it got and whether its time left is above 1 s.
// - callback then restart: W waits with callbacks for 3 s, and its callback blocks on a semaphore.
//   While it blocks, T ends with 0x111 and is started again, so it waits for a second end. Once the
//   callback returns, W waits again for the new run, and T ends with 0x222. Prints whether W waits
//   again for T, then W's result, the status it got and whether its time left is above 1 s.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define T_PRIORITY 0x50
#define W_PRIORITY 0xA0
#define SPINS 400000000

static SceUID go, release, gate, target, waiter, cb;
static volatile int gate_up, gate_open, held, done, use_cb;
static SceUInt w_timeout, w_left;
static int w_result, w_status;

// Ends with the status it was started with once the test signals go
static int target_entry(SceSize args, void *argp) {
    (void)args;
    const int status = *(const int *)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(go, 1, &timeout);
    return status;
}

// Spins on core 0 until the test lets it go
static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

// Starts the gate on core 0 and waits until it spins
static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

// Blocks until the test releases the semaphore, and records that it ran
static int held_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    held = 1;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    return 0;
}

// Waits for T's end with the timeout, with callbacks if use_cb is set, and records what it got
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = w_timeout;
    if (use_cb) {
        cb = sceKernelCreateCallback("cb", 0, held_callback, NULL);
        w_result = sceKernelWaitThreadEndCB(target, &w_status, &timeout);
        sceKernelDeleteCallback(cb);
    } else {
        w_result = sceKernelWaitThreadEnd(target, &w_status, &timeout);
    }
    w_left = timeout;
    done = 1;
    return 0;
}

// Starts W on core 0 to wait for T's end, and waits until W is in that wait
static void start_waiter(SceUInt timeout, int with_cb) {
    w_timeout = timeout;
    use_cb = with_cb;
    done = held = 0;
    w_result = 0xDEAD;
    w_status = 0xDEAD;
    const SceUInt32 type = with_cb ? WAIT_TYPE_CB | WAIT_TYPE_THREAD_END : WAIT_TYPE_THREAD_END;
    waiter = sceKernelCreateThread("w", waiter_entry, W_PRIORITY, 0x4000, 0,
                                   SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(waiter, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(waiter, type, target));
}

// Starts T with status, so it waits for go and then ends with status
static void start_target(int status) {
    sceKernelStartThread(target, sizeof(status), &status);
}

// Creates T on core 1 and starts it
static void new_target(int status) {
    target = sceKernelCreateThread("t", target_entry, T_PRIORITY, 0x4000, 0,
                                   SCE_KERNEL_CPU_MASK_USER_1, NULL);
    start_target(status);
}

// Waits for a thread to end, for at most TEST_WAIT_US, then deletes it
static int join_thread(SceUID thread) {
    SceUInt timeout = TEST_WAIT_US;
    const int result = sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
    return result;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// T ends while the gate holds W, after W's timeout passed
static void timeout_then_end_case(void) {
    const char *name = "timeout then end";
    new_target(0x5A);
    start_waiter(300000, 0);
    gate_close();
    sceKernelDelayThread(700000);
    const SceKernelThreadInfo info = info_of(waiter);
    PRINTF(info.status, "%s: W's status once the timeout passed", name);
    PRINTF(info.waitType, "%s: W's wait type once the timeout passed", name);
    PRINTF(info.waitId == -1, "%s: whether W's wait UID is -1 once the timeout passed", name);
    sceKernelSignalSema(go, 1);
    join_thread(target);
    gate_open = 1;
    join_thread(gate);
    join_thread(waiter);
    PRINTF(w_result, "%s: W's result", name);
    PRINTF(w_status == 0xDEAD, "%s: whether W's status is still 0xDEAD", name);
    PRINTF(w_left == 0, "%s: whether W's time left is 0", name);
}

// T ends at once while the gate holds W, so W gets T's status with its time left still above 1 s
static void end_then_delay_case(void) {
    const char *name = "end then delay";
    new_target(0x5A);
    start_waiter(1500000, 0);
    gate_close();
    sceKernelSignalSema(go, 1);
    join_thread(target);
    sceKernelDelayThread(2000000);
    gate_open = 1;
    join_thread(gate);
    join_thread(waiter);
    PRINTF(w_result, "%s: W's result", name);
    PRINTF(w_status, "%s: the status W got", name);
    PRINTF(w_left > 1000000, "%s: whether W's time left is above 1 s", name);
}

// T ends and is started again while W's callback blocks, then W waits again for the new run
static void callback_restart_case(void) {
    const char *name = "callback then restart";
    new_target(0x111);
    start_waiter(3000000, 1);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(held);
    sceKernelSignalSema(go, 1);
    // The restart needs T's first run to be over, so this waits for its end
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(target, NULL, &timeout);
    start_target(0x222);
    sceKernelSignalSema(release, 1);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_CB | WAIT_TYPE_THREAD_END, target));
    const int waits_again = test_in_wait(waiter, WAIT_TYPE_CB | WAIT_TYPE_THREAD_END, target);
    sceKernelSignalSema(go, 1);
    join_thread(waiter);
    join_thread(target);
    PRINTF(waits_again, "%s: whether W waits again for T", name);
    PRINTF(w_result, "%s: W's result", name);
    PRINTF(w_status, "%s: the status W got", name);
    PRINTF(w_left > 1000000, "%s: whether W's time left is above 1 s", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);

    timeout_then_end_case();
    end_then_delay_case();
    callback_restart_case();

    sceKernelDeleteSema(go);
    sceKernelDeleteSema(release);
    test_exit();
    return 0;
}
