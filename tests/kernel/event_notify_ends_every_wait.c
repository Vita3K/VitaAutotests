// Whether a set with notification ends the waits of all the threads that registered a callback to
// the event before it returns. Three threads A, B and C each create a callback, register it to the
// same simple event E and wait with callbacks. A waits on a semaphore, B waits on E itself for a
// bit the notifying set doesn't give, and C delays. The main thread starts them one after another
// and waits until each one waits, so the registration order is the start order. Then it sets E with
// notification and reads the three threads right after it, with no call that can block in between.
// Then it blocks until all three callbacks ran, and prints. The main thread runs at 0x60 and the
// three threads at 0xB0, all pinned to core 0, so they only run while the main thread blocks. Each
// case has a fresh event, semaphore and threads.
// The cases, by registration order:
// - A, B, C
// - C, B, A
// - B, C, A
// Each case prints the notify's result, then for every thread its status and wait type right
// after the notify, then how many callbacks ran, and the wait result of every thread. A and B end
// by their release after the callbacks, and C ends at its delay.
//
// On a PS Vita TV on firmware 3.65 the notify ends all three waits before it returns, so all three
// threads are ready (status 2, no wait type) right after it, whatever the registration order, and
// all three callbacks run. The order the three threads then run their callbacks in changes from run
// to run, so the test doesn't print it.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_US (3 * TEST_WAIT_US)
#define DELAY_US TEST_WAIT_US

static SceUID event, sema;
static volatile int order_count;
static volatile char order[4];
static volatile int callback_ready[3], wait_result[3];

// Appends the letter of the thread, which is the callback's common pointer, to the order
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    if (order_count < 3)
        order[order_count++] = *(const char *)common;
    return 0;
}

// Creates its callback, registers it to E and waits with callbacks for the wait of its letter
static int waiter(SceSize args, void *argp) {
    (void)args;
    const int index = *(const int *)argp;
    static const char letters[3] = { 'A', 'B', 'C' };
    SceUID callback = sceKernelCreateCallback("callback", 0, callback_entry, (void *)&letters[index]);
    sceKernelRegisterCallbackToEvent(event, callback);
    callback_ready[index] = 1;
    SceUInt timeout = LONG_US;
    SceUInt32 pattern = 0;
    SceUInt64 user_data = 0;
    switch (index) {
    case 0: wait_result[index] = sceKernelWaitSemaCB(sema, 1, &timeout); break;
    case 1: wait_result[index] = sceKernelWaitEventCB(event, EVENT_BIT_B, &pattern, &user_data, &timeout); break;
    default: wait_result[index] = sceKernelDelayThreadCB(DELAY_US); break;
    }
    sceKernelDeleteCallback(callback);
    return 0;
}

static SceUInt32 wait_type_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : NOT_DONE;
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.status : NOT_DONE;
}

// Returns whether the thread of index waits with callbacks in the wait of its letter
static int waits(int index, SceUID thread) {
    switch (index) {
    case 0: return test_in_wait(thread, WAIT_TYPE_SEMA | WAIT_TYPE_CB, sema);
    case 1: return test_in_wait(thread, WAIT_TYPE_EVENT | WAIT_TYPE_CB, event);
    default: return wait_type_of(thread) == (WAIT_TYPE_DELAY | WAIT_TYPE_CB);
    }
}

// Runs one case, where start holds the thread indexes in registration order
static void run_case(const char *name, const int start[3]) {
    static int indexes[3] = { 0, 1, 2 };
    SceUID threads[3];
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_NOTIFY_CB_ALL, 0, NULL);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    order_count = 0;
    order[0] = order[1] = order[2] = '-';
    order[3] = 0;
    for (int i = 0; i < 3; i++) {
        callback_ready[i] = 0;
        wait_result[i] = NOT_DONE;
    }
    for (int i = 0; i < 3; i++) {
        const int index = start[i];
        threads[index] = sceKernelCreateThread("waiter", waiter, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(threads[index], sizeof(int), &indexes[index]);
        TEST_WAIT_FOR(callback_ready[index] && waits(index, threads[index]));
    }
    int waiting_before = 1;
    for (int i = 0; i < 3; i++)
        waiting_before &= waits(i, threads[i]);

    const int notify = sceKernelSetEventWithNotifyCallback(event, EVENT_BIT_A, 0, 0x55);
    int status[3];
    SceUInt32 wait_type[3];
    for (int i = 0; i < 3; i++) {
        status[i] = status_of(threads[i]);
        wait_type[i] = wait_type_of(threads[i]);
    }

    TEST_WAIT_FOR(order_count == 3);
    // A and B wait again after their callbacks, and C waits until its delay ends
    TEST_WAIT_FOR(waits(0, threads[0]) && waits(1, threads[1]));
    sceKernelSignalSema(sema, 1);
    sceKernelSetEvent(event, EVENT_BIT_B, 0);
    for (int i = 0; i < 3; i++)
        test_join_thread(threads[i]);
    sceKernelDeleteSema(sema);
    sceKernelDeleteSimpleEvent(event);

    PRINTF(waiting_before, "%s: the three threads wait before the notify", name);
    PRINTF(notify, "%s: the notify", name);
    PRINTF(status[0], "%s: A's status after the notify", name);
    PRINTF(wait_type[0], "%s: A's wait type after the notify", name);
    PRINTF(status[1], "%s: B's status after the notify", name);
    PRINTF(wait_type[1], "%s: B's wait type after the notify", name);
    PRINTF(status[2], "%s: C's status after the notify", name);
    PRINTF(wait_type[2], "%s: C's wait type after the notify", name);
    PRINTF(order_count, "%s: callbacks run", name);
    PRINTF(wait_result[0], "%s: A's wait result", name);
    PRINTF(wait_result[1], "%s: B's wait result", name);
    PRINTF(wait_result[2], "%s: C's wait result", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    static const int abc[3] = { 0, 1, 2 };
    static const int cba[3] = { 2, 1, 0 };
    static const int bca[3] = { 1, 2, 0 };
    run_case("registered A, B, C", abc);
    run_case("registered C, B, A", cba);
    run_case("registered B, C, A", bca);

    test_exit();
    return 0;
}
