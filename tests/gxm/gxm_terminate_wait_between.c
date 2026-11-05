// What the display queue's worker and a thread inside sceGxmTerminate show between two queued
// callbacks, and whether one worker thread runs every callback. Main queues entry 0 and its callback
// writes a marker into a TLS slot of the thread it runs on and records that thread's ID. Once it
// finished, the test polls the idle worker and prints its status and whether it waits for an event
// flag. Then thread T queues entries 1 and 2 and calls sceGxmTerminate. The callback of entry 1
// blocks on semaphore A. Main reads T, sends it a signal, reads T again and releases A. The callback
// of entry 2 records T's wait, whether it runs on the thread of the first two callbacks, whether the
// TLS marker is still in that thread's slot, then blocks on semaphore B. Main prints those with the number of finished callbacks and whether
// terminate returned, then releases B. Last it prints the result of terminate, how many callbacks
// finished when it returned, whether one thread ran all three callbacks and what
// sceKernelGetThreadInfo gives for that thread. The sync objects stay undestroyed, since the entries
// use them until sceGxmTerminate returns. Main polls T until it waits for a thread's end before it
// reads it. Every wait of main is a bounded poll, and the callbacks block for up to 6 seconds.
//
// On a PS Vita TV on firmware 3.65 the idle worker shows WAITING 0x8 on an event flag. T shows
// WAITING 0x8 with the wait type for a thread's end (0x2) on the worker's thread ID while the first
// callback holds, after the signal, which returns 0, and during the second callback. While the second
// callback holds, two callbacks have finished and terminate hasn't returned. All three callbacks run
// on one thread, which keeps the TLS marker. Terminate returns 0 after all three callbacks finished,
// and then sceKernelGetThreadInfo on the worker fails with 0x80028021.
#include <psp2/gxm.h>

#include "test.h"

#define ENTRIES 3
#define SYNCS (2 * ENTRIES)
#define SEMA_TIMEOUT_US (3 * TEST_WAIT_US)
// The TLS slot the first callback writes. The system uses the low slots.
#define MARKER_SLOT 200
#define MARKER 0x5A5A1234

static SceGxmSyncObject *syncs[SYNCS];
static SceUID sema_a, sema_b;
static SceUID terminator_thread;
static SceUID callback_thread[ENTRIES];
static volatile int started[ENTRIES], finished;

// What the second callback saw
static volatile int c2_get_info, c2_status, c2_wait_type, c2_waits_for_worker;
static volatile int c2_same_as_0, c2_same_as_1, c2_marker_kept;

static volatile int add_result[2], terminate_result = -1, terminate_done, finished_at_return;

static void display_callback(const void *data) {
    const int entry = *(const int *)data;
    callback_thread[entry] = sceKernelGetThreadId();
    if (entry == 0) {
        *(volatile SceUInt32 *)sceKernelGetTLSAddr(MARKER_SLOT) = MARKER;
    } else if (entry == 2) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        c2_get_info = sceKernelGetThreadInfo(terminator_thread, &info);
        c2_status = info.status;
        c2_wait_type = info.waitType;
        c2_waits_for_worker = info.waitId == callback_thread[0];
        c2_same_as_0 = callback_thread[2] == callback_thread[0];
        c2_same_as_1 = callback_thread[2] == callback_thread[1];
        c2_marker_kept = *(volatile SceUInt32 *)sceKernelGetTLSAddr(MARKER_SLOT) == MARKER;
    }
    // Set after what the second callback records, so main reads all of it once it sees this
    started[entry] = 1;
    if (entry == 1) {
        SceUInt timeout = SEMA_TIMEOUT_US;
        sceKernelWaitSema(sema_a, 1, &timeout);
    } else if (entry == 2) {
        SceUInt timeout = SEMA_TIMEOUT_US;
        sceKernelWaitSema(sema_b, 1, &timeout);
    }
    finished++;
}

static int terminator(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int entry = 1; entry <= 2; entry++)
        add_result[entry - 1] = sceGxmDisplayQueueAddEntry(syncs[2 * entry], syncs[2 * entry + 1], &entry);
    terminate_result = sceGxmTerminate();
    finished_at_return = finished;
    terminate_done = 1;
    return 0;
}

// Polls the thread for up to one second until it is in a wait of the given type, then reads its info
static int read_waiting(SceUID thread, SceUInt32 type, SceKernelThreadInfo *info) {
    int result = -1;
    for (int i = 0; i < 1000; i++) {
        info->size = sizeof(*info);
        result = sceKernelGetThreadInfo(thread, info);
        if (result < 0 || info->waitType == type)
            break;
        sceKernelDelayThread(1000);
    }
    return result;
}

int main(void) {
    sema_a = sceKernelCreateSema("a", 0, 0, 1, NULL);
    sema_b = sceKernelCreateSema("b", 0, 0, 1, NULL);

    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 3;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = sizeof(int);
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));
    int created = 0;
    for (int i = 0; i < SYNCS; i++)
        created |= sceGxmSyncObjectCreate(&syncs[i]);
    PRINT("create the sync objects", created);

    // The idle worker
    int entry0 = 0;
    const int add0 = sceGxmDisplayQueueAddEntry(syncs[0], syncs[1], &entry0);
    TEST_WAIT_FOR(finished == 1);
    const int callback0_finished = finished == 1;
    const SceUID worker = callback_thread[0];
    SceKernelThreadInfo worker_info = { 0 };
    const int worker_result = read_waiting(worker, WAIT_TYPE_EVENT_FLAG, &worker_info);
    PRINT("add entry 0", add0);
    PRINT("callback 0 finished", callback0_finished);
    PRINT("the idle worker's info", worker_result);
    PRINT("the idle worker's status", worker_info.status);
    PRINT("the idle worker waits for an event flag", worker_info.waitType == WAIT_TYPE_EVENT_FLAG);

    // T queues the other two entries and terminates
    terminator_thread = sceKernelCreateThread("terminator", terminator, SCE_KERNEL_DEFAULT_PRIORITY_USER - 2, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(terminator_thread, 0, NULL);
    TEST_WAIT_FOR(started[1]);
    const int callback1_started = started[1];
    SceKernelThreadInfo before = { 0 };
    const int before_result = read_waiting(terminator_thread, WAIT_TYPE_THREAD_END, &before);
    const int signal_result = sceKernelSendSignal(terminator_thread);
    SceKernelThreadInfo after = { .size = sizeof(after) };
    const int after_result = sceKernelGetThreadInfo(terminator_thread, &after);
    PRINT("first callback started", callback1_started);
    PRINT("add entry 1", add_result[0]);
    PRINT("add entry 2", add_result[1]);
    PRINT("T's info", before_result);
    PRINT("T's status", before.status);
    PRINT("T's wait type", before.waitType);
    PRINT("T waits for the worker", before.waitId == worker);
    PRINT("signal T", signal_result);
    PRINT("T's info after the signal", after_result);
    PRINT("T's status after the signal", after.status);
    PRINT("T's wait type after the signal", after.waitType);
    PRINT("T waits for the worker after the signal", after.waitId == worker);
    sceKernelSignalSema(sema_a, 1);

    // Between the two callbacks
    TEST_WAIT_FOR(started[2]);
    const int callback2_started = started[2];
    const int finished_between = finished;
    const int terminate_between = terminate_done;
    PRINT("second callback started", callback2_started);
    PRINT("callbacks finished while the second one holds", finished_between);
    PRINT("terminate returned while the second callback holds", terminate_between);
    PRINT("the second callback read T's info", c2_get_info);
    PRINT("T's status in the second callback", c2_status);
    PRINT("T's wait type in the second callback", c2_wait_type);
    PRINT("T waits for the worker in the second callback", c2_waits_for_worker);
    PRINT("the second callback runs on the thread of callback 0", c2_same_as_0);
    PRINT("the second callback runs on the thread of callback 1", c2_same_as_1);
    PRINT("the TLS marker of callback 0 is still there", c2_marker_kept);
    sceKernelSignalSema(sema_b, 1);

    // After terminate
    TEST_WAIT_FOR(terminate_done);
    const int done = terminate_done;
    const int one_thread = callback_thread[0] == callback_thread[1] && callback_thread[1] == callback_thread[2];
    SceKernelThreadInfo gone = { .size = sizeof(gone) };
    int gone_result = 0;
    // Polls for one second at most for the worker to be gone
    for (int i = 0; i < 1000; i++) {
        gone_result = sceKernelGetThreadInfo(worker, &gone);
        if (gone_result < 0)
            break;
        sceKernelDelayThread(1000);
    }
    PRINT("terminate returned", done);
    PRINT("terminate", terminate_result);
    PRINT("callbacks finished when terminate returned", finished_at_return);
    PRINT("one thread ran all three callbacks", one_thread);
    PRINT("the worker's info after terminate", gone_result);

    test_join_thread(terminator_thread);
    sceKernelDeleteSema(sema_a);
    sceKernelDeleteSema(sema_b);
    test_exit();
    return 0;
}
