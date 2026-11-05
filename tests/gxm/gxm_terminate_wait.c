// What a thread inside sceGxmTerminate waits for, and whether the display queue's worker waits
// before it runs a callback that was still queued. The display callback of entry 1 blocks on a
// semaphore until the test releases it, so the worker holds entry 1 in its callback and entry 2
// queued behind it. Thread T adds both entries and calls sceGxmTerminate. The test polls T until it
// waits for a thread's end, prints what T's wait is, and checks that neither callback ran further
// and terminate didn't return. Then it signals the semaphore and prints what terminate returned
// after, and whether the worker left at least 9 ms between the signal and the start of the callback
// of entry 2. The cases:
// - sceGxmInitialize with the display queue and four sync objects, one pair per entry
// - T's wait while the first callback holds the worker: status, wait type and whether it waits for
//   the worker
// - terminate and the second callback while the first one holds: neither is done
// - once the semaphore is signaled: the result of terminate, the callbacks finished when it
//   returned, their order and thread, and the gap
// The sync objects stay undestroyed, since the entries use them until sceGxmTerminate returns and
// destroying them after it isn't what this tests.
//
// On a PS Vita TV on firmware 3.65 T shows WAITING 0x8 with the wait type for a thread's end (0x2),
// on the worker's thread ID. While the first callback holds the worker, the second callback hasn't
// started and terminate hasn't returned. After the signal terminate returns 0 after both callbacks
// have finished. Both callbacks run on the same thread and entry 1 runs first. The gap between the
// signal and the start of the second callback is less than 9 ms.
#include <psp2/gxm.h>

#include "test.h"

#define PAIRS 4
#define SEMA_TIMEOUT_US (3 * TEST_WAIT_US)

static SceGxmSyncObject *syncs[PAIRS];
static SceUID release;
static SceUID worker; // The thread that runs the callbacks, set by the first one
static volatile int started[3], finished;
static volatile int order[2], same_thread = 1;
static volatile SceUInt64 start_time[3];

static volatile int add_result[2], terminate_result = -1, terminate_done, finished_at_return;

static void display_callback(const void *data) {
    const int entry = *(const int *)data;
    if (entry == 1)
        worker = sceKernelGetThreadId();
    else if (sceKernelGetThreadId() != worker)
        same_thread = 0;
    order[finished] = entry;
    start_time[entry] = sceKernelGetProcessTimeWide();
    started[entry] = 1;
    if (entry == 1) {
        SceUInt timeout = SEMA_TIMEOUT_US;
        sceKernelWaitSema(release, 1, &timeout);
    }
    finished++;
}

static int terminator(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (int entry = 1; entry <= 2; entry++)
        add_result[entry - 1] = sceGxmDisplayQueueAddEntry(syncs[2 * entry - 2], syncs[2 * entry - 1], &entry);
    terminate_result = sceGxmTerminate();
    finished_at_return = finished;
    terminate_done = 1;
    return 0;
}

int main(void) {
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);

    SceGxmInitializeParams init = { 0 };
    init.displayQueueMaxPendingCount = 3;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = sizeof(int);
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    PRINT("initialize libgxm", sceGxmInitialize(&init));
    int created = 0;
    for (int i = 0; i < PAIRS; i++)
        created |= sceGxmSyncObjectCreate(&syncs[i]);
    PRINT("create the sync objects", created);

    const SceUID thread = test_start_thread(terminator);
    TEST_WAIT_FOR(started[1]);
    PRINT("first callback started", started[1]);
    // Polls for one second at most, well under what the first callback waits for the semaphore
    for (int i = 0; i < 1000 && !test_in_wait(thread, WAIT_TYPE_THREAD_END, worker); i++)
        sceKernelDelayThread(1000);

    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINT("add entry 1", add_result[0]);
    PRINT("add entry 2", add_result[1]);
    PRINT("T's status", info.status);
    PRINT("T's wait type", info.waitType);
    PRINT("T waits for the worker", info.waitId == worker);
    PRINT("terminate returned", terminate_done);
    PRINT("second callback started", started[2]);

    const SceUInt64 signaled = sceKernelGetProcessTimeWide();
    sceKernelSignalSema(release, 1);
    TEST_WAIT_FOR(terminate_done);
    PRINT("terminate returned now", terminate_done);
    PRINT("terminate", terminate_result);
    PRINT("terminate returned after both callbacks", finished_at_return == 2);
    PRINT("callbacks ran in queue order", order[0] == 1 && order[1] == 2);
    PRINT("both callbacks ran on the same thread", same_thread);
    PRINT("the gap before the leftover callback is at least 9 ms", start_time[2] - signaled >= 9000);

    test_join_thread(thread);
    sceKernelDeleteSema(release);
    test_exit();
    return 0;
}
