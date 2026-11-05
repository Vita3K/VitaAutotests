// Whether the hold of a ForVM suspend is taken once for the first callback of a pass or at the start
// of every callback. T waits on a semaphore with callbacks, on core 0 with the VM attribute, and the
// main thread on core 1 suspends it twice. A spinner of a higher priority on core 0 keeps T from
// running while the main thread notifies callback A, then callback B, so both are pending when T
// gets to run. Then the spinner ends and T holds, and the main thread prints what it reads:
// - the notify counts of A and B while T can't run yet
// - the lowest notify count of each over 50 ms while T is held, and the callbacks' calls
// - T's status, the counts and the calls again after one resume, which only lowers the suspend count
// - after the second resume the order the callbacks ran in, the counts they got and T's wait result
//
// On a PS Vita TV on firmware 3.65 the notify counts of A and B are both 1 while T can't run. Once
// T is held with status 0x100 and wait type 0, A's lowest notify count is 0 and B's is 1, and no
// callback has run. After one resume, which returns 1, T is still held with B's count at 1 and no
// callback has run. After the second resume, which returns 0, A runs and then B, each once with a
// count of 1, and T's wait returns 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0xA0
#define SPINNER_PRIORITY 0x90

static SceUID sema, callback_a, callback_b;
static volatile int ran, woke, wait_result, spinning, release;
static volatile int calls_a, calls_b, count_a, count_b, order_len;
static volatile int order[4];

static void record(int id) {
    if (order_len < 4)
        order[order_len] = id;
    order_len++;
}

static int callback_a_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)arg;
    (void)common;
    count_a = count;
    calls_a++;
    record(1);
    return 0;
}

static int callback_b_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)arg;
    (void)common;
    count_b = count;
    calls_b++;
    record(2);
    return 0;
}

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback_a = sceKernelCreateCallback("cb_a", 0, callback_a_entry, NULL);
    callback_b = sceKernelCreateCallback("cb_b", 0, callback_b_entry, NULL);
    ran = 1;
    SceUInt timeout = 3 * TEST_WAIT_US;
    wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    woke = 1;
    return 0;
}

// Spins until released, for at most 5 seconds
static int spinner_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const SceUInt64 end = sceKernelGetProcessTimeWide() + 5 * 1000000;
    spinning = 1;
    while (!release && sceKernelGetProcessTimeWide() < end) {
    }
    return 0;
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

static int notify_count(SceUID callback) {
    CallbackInfo info = { .size = sizeof(info) };
    const int result = sceKernelGetCallbackInfo(callback, (SceKernelCallbackInfo *)&info);
    return result < 0 ? result : info.notifyCount;
}

// Reads the lowest notify count of both callbacks over 50 ms
static void lowest_notify_counts(int *lowest_a, int *lowest_b) {
    *lowest_a = notify_count(callback_a);
    *lowest_b = notify_count(callback_b);
    for (int i = 0; i < 50; i++) {
        sceKernelDelayThread(1000);
        const int a = notify_count(callback_a), b = notify_count(callback_b);
        if (a < *lowest_a)
            *lowest_a = a;
        if (b < *lowest_b)
            *lowest_b = b;
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    wait_result = 0xDEAD;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(ran);
    test_wait_until_waiting(t);

    const SceUID spinner = sceKernelCreateThread("spinner", spinner_entry, SPINNER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(spinner, 0, NULL);
    TEST_WAIT_FOR(spinning);

    const int suspended_1 = sceKernelSuspendThreadForVM(t);
    const int suspended_2 = sceKernelSuspendThreadForVM(t);
    // T leaves its wait for the notify and can't run, since the spinner keeps core 0
    sceKernelNotifyCallback(callback_a, 1);
    TEST_WAIT_FOR(info_of(t).waitType == 0);
    sceKernelNotifyCallback(callback_b, 1);
    const int pending_a = notify_count(callback_a), pending_b = notify_count(callback_b);

    // T runs now, takes A's notification and holds at the start of A
    release = 1;
    TEST_WAIT_FOR(info_of(t).status & 0x100);
    const SceKernelThreadInfo held = info_of(t);
    int lowest_a, lowest_b;
    lowest_notify_counts(&lowest_a, &lowest_b);
    const int held_calls = calls_a + calls_b;

    const int resumed_1 = sceKernelResumeThreadForVM(t);
    sceKernelDelayThread(50000);
    const SceKernelThreadInfo still_held = info_of(t);
    const int still_count_b = notify_count(callback_b);
    const int still_calls = calls_a + calls_b;

    const int resumed_2 = sceKernelResumeThreadForVM(t);
    TEST_WAIT_FOR(calls_a + calls_b >= 2);
    test_wait_until_waiting(t);
    const int final_calls_a = calls_a, final_calls_b = calls_b;
    const int final_count_a = count_a, final_count_b = count_b;
    const int ran_order = order[0] * 16 + order[1];

    sceKernelSignalSema(sema, 1);
    test_join_thread(t);
    sceKernelDeleteSema(sema);
    test_join_thread(spinner);

    PRINT("suspend it", suspended_1);
    PRINT("suspend it again", suspended_2);
    PRINT("both notified while T can't run: A's notify count", pending_a);
    PRINT("both notified while T can't run: B's notify count", pending_b);
    PRINT("held: T's status", held.status);
    PRINT("held: T's wait type", held.waitType);
    PRINT("held: the lowest notify count of A", lowest_a);
    PRINT("held: the lowest notify count of B", lowest_b);
    PRINT("held: the callbacks' calls", held_calls);
    PRINT("resume it", resumed_1);
    PRINT("after one resume: T's status", still_held.status);
    PRINT("after one resume: T's wait type", still_held.waitType);
    PRINT("after one resume: B's notify count", still_count_b);
    PRINT("after one resume: the callbacks' calls", still_calls);
    PRINT("resume it again", resumed_2);
    PRINT("after both resumes: A's calls", final_calls_a);
    PRINT("after both resumes: B's calls", final_calls_b);
    PRINT("after both resumes: the order they ran in, A is 1 and B is 2", ran_order);
    PRINT("after both resumes: the count A got", final_count_a);
    PRINT("after both resumes: the count B got", final_count_b);
    PRINT("its wait's result", wait_result);
    test_exit();
    return 0;
}
