// What a ForVM suspend does to a signal wait and a thread end wait whose 300 ms timeout passes
// while the thread is suspended, when a signal is sent or the target ends after the timeout and
// before the resume. Also the callback forms of a signal wait, a thread end wait and a delay, with
// a 2 s timeout, notified while the thread is suspended for a second. The waiter W has the VM
// attribute and runs on core 0, and the main thread on core 1 suspends it. The target of a thread
// end wait runs on core 1 and ends when main stops it, or on its own after ten seconds. Every case
// reads what it prints before it prints:
// - a signal wait, suspended for 700 ms so its timeout passes, then a signal sent and the resume:
//   W's status and wait type while suspended, its wait's result, and the result of a WaitSignal
//   with a timeout of 0 that W makes after its wait, which takes the signal only if the send left
//   one pending
// - a thread end wait, suspended the same way, then the target ends before the resume: W's status
//   and wait type while suspended, its wait's result, whether the status variable it was given is
//   still 0xDEAD, and the same WaitSignal with a timeout of 0
// - a callback in a signal wait, in a thread end wait and in a delay: the runs of the callback and
//   W's status and wait type while suspended, then the resume, the runs after it, the wait type
//   once the callback returned, its wait's result, and whether it returned 1.5 s to 2.5 s after its
//   wait began, which is its deadline kept across the suspend
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define W_PRIORITY 0xA0

// The plain waits time out while W is suspended, which lasts past them
#define TIMEOUT_US 300000
#define SUSPEND_US 700000

// The callback waits last two seconds, and W stays suspended for one of them
#define CB_TIMEOUT_US 2000000
#define CB_SUSPEND_US 1000000

enum Mode { PLAIN_SIGNAL, PLAIN_END, CB_SIGNAL, CB_END, CB_DELAY };

static volatile SceUID target, callback_uid;
static volatile int mode, woke, wait_result, second_result, end_status, cb_calls, stop_target;
static volatile SceUInt64 wait_began, wait_ended;

// Counts the runs of the callback
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    cb_calls++;
    return 0;
}

// Ends once stop_target is set, or on its own after ten seconds, and returns 0x5A
static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int n = 0; n < 1000 && !stop_target; n++)
        sceKernelDelayThread(10000);
    return 0x5A;
}

// Waits in the mode it is given, then records its result, and after a plain wait a WaitSignal with
// a timeout of 0
static int waiter_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int status = 0xDEAD;
    SceUInt32 timeout = 0;
    if (mode == CB_SIGNAL || mode == CB_END || mode == CB_DELAY)
        callback_uid = sceKernelCreateCallback("cb", 0, callback_entry, NULL);
    wait_began = sceKernelGetProcessTimeWide();
    switch (mode) {
    case PLAIN_SIGNAL:
        timeout = TIMEOUT_US;
        wait_result = sceKernelWaitSignal(0, 0, &timeout);
        break;
    case PLAIN_END:
        timeout = TIMEOUT_US;
        wait_result = sceKernelWaitThreadEnd(target, &status, &timeout);
        break;
    case CB_SIGNAL:
        timeout = CB_TIMEOUT_US;
        wait_result = sceKernelWaitSignalCB(0, 0, &timeout);
        break;
    case CB_END:
        timeout = CB_TIMEOUT_US;
        wait_result = sceKernelWaitThreadEndCB(target, &status, &timeout);
        break;
    case CB_DELAY: wait_result = sceKernelDelayThreadCB(CB_TIMEOUT_US); break;
    }
    wait_ended = sceKernelGetProcessTimeWide();
    end_status = status;
    if (mode == PLAIN_SIGNAL || mode == PLAIN_END) {
        SceUInt32 zero = 0;
        second_result = sceKernelWaitSignal(0, 0, &zero);
    }
    woke = 1;
    return 0;
}

static SceUInt32 status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.status;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.waitType;
}

// Prints the status and the wait type of thread, under name
static void print_state(const char *name, SceUID thread) {
    PRINTF(status_of(thread), "%s: its status", name);
    PRINTF(wait_type_of(thread), "%s: its wait type", name);
}

// Starts a waiter in mode, on core 0 with the VM attribute, with its results reset
static SceUID start_waiter(int new_mode) {
    mode = new_mode;
    woke = 0;
    wait_result = second_result = end_status = 0xDEAD;
    cb_calls = 0;
    callback_uid = 0;
    const SceUID w = sceKernelCreateThread("w", waiter_entry, W_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    return w;
}

// Starts the target of a thread end wait, on core 1
static SceUID start_target(void) {
    stop_target = 0;
    const SceUID t = sceKernelCreateThread("target", target_entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(t, 0, NULL);
    return t;
}

// A signal wait whose timeout passes while W is suspended, then a signal sent before the resume
static void signal_timeout(void) {
    const SceUID w = start_waiter(PLAIN_SIGNAL);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_SIGNAL);
    PRINT("a signal wait: suspend it", sceKernelSuspendThreadForVM(w));
    sceKernelDelayThread(SUSPEND_US);
    print_state("a signal wait suspended past its timeout", w);
    PRINT("a signal wait suspended past its timeout: send it a signal", sceKernelSendSignal(w));
    PRINT("a signal wait suspended past its timeout: resume it", sceKernelResumeThreadForVM(w));
    TEST_WAIT_FOR(woke);
    PRINT("a signal wait suspended past its timeout: its wait", wait_result);
    PRINT("a signal wait suspended past its timeout: its WaitSignal with a timeout of 0", second_result);
    test_join_thread(w);
}

// A thread end wait whose timeout passes while W is suspended, then the target ends before the
// resume
static void end_timeout(void) {
    target = start_target();
    const SceUID w = start_waiter(PLAIN_END);
    TEST_WAIT_FOR(wait_type_of(w) == WAIT_TYPE_THREAD_END);
    PRINT("a thread end wait: suspend it", sceKernelSuspendThreadForVM(w));
    sceKernelDelayThread(SUSPEND_US);
    print_state("a thread end wait suspended past its timeout", w);
    stop_target = 1;
    test_join_thread(target);
    PRINT("a thread end wait suspended past its timeout: resume it", sceKernelResumeThreadForVM(w));
    TEST_WAIT_FOR(woke);
    PRINT("a thread end wait suspended past its timeout: its wait", wait_result);
    PRINT("a thread end wait suspended past its timeout: the status variable is untouched", end_status == 0xDEAD);
    PRINT("a thread end wait suspended past its timeout: its WaitSignal with a timeout of 0", second_result);
    test_join_thread(w);
}

// A wait with callbacks, suspended for a second and notified while suspended, then resumed. The
// callback should run only after the resume, and the wait should end at its deadline.
static void callback_form(const char *name, int form, SceUInt32 waiting_type) {
    if (form == CB_END)
        target = start_target();
    const SceUID w = start_waiter(form);
    TEST_WAIT_FOR(wait_type_of(w) == waiting_type);
    PRINTF(sceKernelSuspendThreadForVM(w), "%s: suspend it", name);
    sceKernelNotifyCallback(callback_uid, 1);
    sceKernelDelayThread(CB_SUSPEND_US);
    PRINTF(cb_calls, "%s: runs of the callback while suspended", name);
    PRINTF(status_of(w), "%s: its status while suspended", name);
    PRINTF(wait_type_of(w), "%s: its wait type while suspended", name);
    PRINTF(sceKernelResumeThreadForVM(w), "%s: resume it", name);
    TEST_WAIT_FOR(cb_calls > 0);
    PRINTF(cb_calls, "%s: runs of the callback after the resume", name);
    TEST_WAIT_FOR(wait_type_of(w) == waiting_type);
    PRINTF(wait_type_of(w), "%s: its wait type once the callback returned", name);
    TEST_WAIT_FOR(woke);
    PRINTF(wait_result, "%s: its wait", name);
    const SceUInt64 elapsed = wait_ended - wait_began;
    PRINTF(elapsed >= 1500000 && elapsed < 2500000, "%s: it returned at its deadline", name);
    test_join_thread(w);
    if (form == CB_END) {
        stop_target = 1;
        test_join_thread(target);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    signal_timeout();
    end_timeout();
    callback_form("a callback in a signal wait", CB_SIGNAL, WAIT_TYPE_SIGNAL | WAIT_TYPE_CB);
    callback_form("a callback in a thread end wait", CB_END, WAIT_TYPE_THREAD_END | WAIT_TYPE_CB);
    callback_form("a callback in a delay", CB_DELAY, WAIT_TYPE_DELAY | WAIT_TYPE_CB);
    test_exit();
    return 0;
}
