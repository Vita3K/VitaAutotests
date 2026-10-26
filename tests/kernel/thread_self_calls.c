// What StartThread, DeleteThread and GetThreadExitStatus return for UID 0 and for the calling
// thread's own UID, and whether those calls change the caller. The calls come from four places: the
// main thread, a child thread pinned to core 0, and a callback in each of the two. The cases:
// - the main thread and the child each call StartThread with UID 0 and with their own UID, then
//   GetThreadExitStatus with UID 0 and a status pointer, with UID 0 and no pointer, and with their
//   own UID and a status pointer, and the status variable after each call with a pointer
// - a callback in the main thread and one in the child make the same calls, without DeleteThread,
//   since a callback must never delete the thread it runs in
// - the child calls StartThread, GetThreadExitStatus and WaitThreadEnd with a 300 ms timeout on the
//   main thread's UID, which is running
// - the status and exit status of the main thread and the child, read before and after the calls,
//   are compared
// The destructive calls go last. The main thread prints the child's results first, then the child
// calls DeleteThread on the main thread's UID and on UID 0, and the main thread calls DeleteThread
// on UID 0 as the last call of the test, since a wrong implementation could delete the caller with
// it.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CHILD_PRIORITY 0xA0

// The status and exit status of a thread, as its info reports them
typedef struct ThreadStatus {
    SceUInt32 status;
    int exit_status;
} ThreadStatus;

// What the calls on UID 0 and on the own UID returned, and the status variable after each call
// that takes a pointer
typedef struct SelfCalls {
    int start_zero;
    int start_own;
    int exit_zero;
    int exit_zero_status;
    int exit_zero_null;
    int exit_own;
    int exit_own_status;
} SelfCalls;

static SceUID main_thread, wait_sema, go_sema, child_cb;
static volatile int child_cb_ready, child_cb_runs, child_stage1_done;
static volatile int main_cb_runs;
static SelfCalls main_cb_calls, child_calls, child_cb_calls;
static ThreadStatus child_before, child_after;
static int child_main_start, child_main_exit, child_main_status, child_main_wait;
static int child_wait, child_delete_main, child_delete_zero;

// Returns the status and exit status of a thread
static ThreadStatus status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    ThreadStatus result = { info.status, info.exitStatus };
    return result;
}

// Makes the calls on UID 0 and on the calling thread's own UID, and records what they return
static void self_calls(SelfCalls *calls) {
    const SceUID self = sceKernelGetThreadId();
    int zero_status = 0xDEAD, own_status = 0xDEAD;
    calls->start_zero = sceKernelStartThread(0, 0, NULL);
    calls->start_own = sceKernelStartThread(self, 0, NULL);
    calls->exit_zero = sceKernelGetThreadExitStatus(0, &zero_status);
    calls->exit_zero_status = zero_status;
    calls->exit_zero_null = sceKernelGetThreadExitStatus(0, NULL);
    calls->exit_own = sceKernelGetThreadExitStatus(self, &own_status);
    calls->exit_own_status = own_status;
}

// Prints the results of one thread's self calls, under a label for that thread
static void print_self_calls(const char *who, const SelfCalls *calls) {
    PRINTF(calls->start_zero, "%s: StartThread with UID 0", who);
    PRINTF(calls->start_own, "%s: StartThread with its own UID", who);
    PRINTF(calls->exit_zero, "%s: GetThreadExitStatus with UID 0 and a status", who);
    PRINTF(calls->exit_zero_status, "%s: the status variable after the UID 0 call", who);
    PRINTF(calls->exit_zero_null, "%s: GetThreadExitStatus with UID 0 and no status", who);
    PRINTF(calls->exit_own, "%s: GetThreadExitStatus with its own UID and a status", who);
    PRINTF(calls->exit_own_status, "%s: the status variable after the own UID call", who);
}

// Makes the self calls in the main thread when a wait with callbacks runs the callback
static int main_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    self_calls(&main_cb_calls);
    main_cb_runs++;
    return 0;
}

// Makes the self calls in the child when its wait with callbacks runs the callback
static int child_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    self_calls(&child_cb_calls);
    child_cb_runs++;
    return 0;
}

// Makes its calls, then waits with callbacks for the callback the main thread notifies, and then
// waits for the main thread's go before the calls that could delete a thread
static int child_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    child_before = status_of(sceKernelGetThreadId());
    self_calls(&child_calls);

    // The main thread is alive and running, so these calls target a live thread
    child_main_start = sceKernelStartThread(main_thread, 0, NULL);
    int main_status = 0xDEAD;
    child_main_exit = sceKernelGetThreadExitStatus(main_thread, &main_status);
    child_main_status = main_status;
    SceUInt timeout = 300000;
    child_main_wait = sceKernelWaitThreadEnd(main_thread, NULL, &timeout);

    child_cb = sceKernelCreateCallback("child", 0, child_callback, NULL);
    child_cb_ready = 1;
    timeout = TEST_WAIT_US;
    child_wait = sceKernelWaitSemaCB(wait_sema, 1, &timeout);
    child_stage1_done = 1;

    timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(go_sema, 1, &timeout);
    child_delete_main = sceKernelDeleteThread(main_thread);
    child_delete_zero = sceKernelDeleteThread(0);
    child_after = status_of(sceKernelGetThreadId());
    sceKernelDeleteCallback(child_cb);
    return 0;
}

int main(void) {
    main_thread = sceKernelGetThreadId();
    wait_sema = sceKernelCreateSema("wait", 0, 0, 1, NULL);
    go_sema = sceKernelCreateSema("go", 0, 0, 1, NULL);
    const SceUID main_cb = sceKernelCreateCallback("main", 0, main_callback, NULL);

    const ThreadStatus main_before = status_of(main_thread);
    SelfCalls main_calls;
    self_calls(&main_calls);
    print_self_calls("main", &main_calls);

    // A callback notified before the call runs when the call checks callbacks
    sceKernelNotifyCallback(main_cb, 1);
    sceKernelDelayThreadCB(1000);
    PRINT("main callback: it ran", main_cb_runs);
    print_self_calls("main callback", &main_cb_calls);

    const SceUID child = sceKernelCreateThread("child", child_entry, CHILD_PRIORITY, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(child, 0, NULL);
    TEST_WAIT_FOR(child_cb_ready);
    test_wait_until_waiting(child);
    sceKernelNotifyCallback(child_cb, 1);
    TEST_WAIT_FOR(child_cb_runs);
    sceKernelSignalSema(wait_sema, 1);
    TEST_WAIT_FOR(child_stage1_done);

    // The child is blocked on the go semaphore now, so its results can be read
    print_self_calls("child", &child_calls);
    PRINT("child: StartThread with the main thread's UID", child_main_start);
    PRINT("child: GetThreadExitStatus with the main thread's UID", child_main_exit);
    PRINT("child: the status variable after that call", child_main_status);
    PRINT("child: WaitThreadEnd with the main thread's UID and 300 ms", child_main_wait);
    PRINT("child: its wait with callbacks, after the callback", child_wait);
    PRINT("child callback: it ran", child_cb_runs);
    print_self_calls("child callback", &child_cb_calls);

    sceKernelSignalSema(go_sema, 1);
    test_join_thread(child);
    PRINT("child: DeleteThread with the main thread's UID", child_delete_main);
    PRINT("child: DeleteThread with UID 0", child_delete_zero);
    PRINT("child: its status afterward", child_after.status);
    PRINT("child: its exit status afterward", child_after.exit_status);
    PRINT("child: its status and exit status unchanged",
          child_after.status == child_before.status &&
              child_after.exit_status == child_before.exit_status);

    const ThreadStatus main_after = status_of(main_thread);
    PRINT("main: its status afterward", main_after.status);
    PRINT("main: its exit status afterward", main_after.exit_status);
    PRINT("main: its status and exit status unchanged",
          main_after.status == main_before.status &&
              main_after.exit_status == main_before.exit_status);

    sceKernelDeleteCallback(main_cb);
    sceKernelDeleteSema(wait_sema);
    sceKernelDeleteSema(go_sema);

    // The last call, which a wrong implementation could take for a delete of this thread
    PRINT("main: DeleteThread with UID 0", sceKernelDeleteThread(0));
    test_exit();
    return 0;
}
