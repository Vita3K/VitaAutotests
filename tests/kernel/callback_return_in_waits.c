// What the return value of a callback does inside a wait with callbacks. The cases check whether a
// nonzero return deletes the callback, and whether the wait it ran in keeps its own result:
// - a callback notified while its thread waits on a semaphore returns -1. The wait should go on,
//   so a later signal makes it return 0. The thread then notifies the callback, which should give
//   an error since the callback is gone.
// - a callback pending when a semaphore wait with callbacks starts returns 1. The take should
//   succeed at once and the wait should return 0.
// - a callback that notifies itself on its first run and returns -1 on its second. The wait has a
//   timeout of 0 and can't take what it waits for, so the second run comes after the wait has
//   timed out. The result should stay the timeout, 0x80028005. The same for a mutex lock with
//   callbacks, where another thread holds the mutex.
// - a callback notified while its thread sleeps in sceKernelDelayThreadCB, or waits for a signal
//   with sceKernelWaitSignalCB, returns -1. The wait should go on to its deadline and keep its
//   result, 0 for the delay and the timeout for the signal wait.
// Every case has fresh objects, threads and callbacks. Each case prints after its threads ended,
// and every wait has a finite timeout.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define DELAY_US 2000000
#define SIGNAL_US 2000000
// A wait that ended at its deadline took at least this long
#define ENOUGH_US 1000000

static SceUID callback, sema, mutex, release_sema;
static volatile int runs, wait_result, notify_after;
static volatile SceUInt64 elapsed;

// Counts its run and returns -1 on every run
static int returns_minus_one(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    return -1;
}

// Counts its run and returns 1 on every run
static int returns_one(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    return 1;
}

// Notifies itself on its first run, so it is pending again, and returns -1 on its second run
static int self_then_minus_one(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    if (runs == 1) {
        sceKernelNotifyCallback(callback, 1);
        return 0;
    }
    return -1;
}

// Returns whether thread is in a wait with callbacks of the given type, whatever the object is
static int waits_with_callbacks(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == (type | WAIT_TYPE_CB);
}

// Waits on the semaphore with callbacks, then notifies its callback
static int sema_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("minus", 0, returns_minus_one, NULL);
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSemaCB(sema, 1, &timeout);
    notify_after = sceKernelNotifyCallback(callback, 1);
    return 0;
}

// Holds the mutex until the release semaphore is signaled
static int holder_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelLockMutex(mutex, 1, NULL);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(release_sema, 1, &timeout);
    sceKernelUnlockMutex(mutex, 1);
    return 0;
}

// Creates a callback, then sleeps in a delay with callbacks, and notifies the callback after it
static int delay_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("minus", 0, returns_minus_one, NULL);
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    wait_result = sceKernelDelayThreadCB(DELAY_US);
    elapsed = sceKernelGetProcessTimeWide() - start;
    notify_after = sceKernelNotifyCallback(callback, 1);
    return 0;
}

// Creates a callback, then waits for a signal with callbacks, and notifies the callback after it
static int signal_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    callback = sceKernelCreateCallback("minus", 0, returns_minus_one, NULL);
    SceUInt32 timeout = SIGNAL_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    wait_result = sceKernelWaitSignalCB(0, 0, &timeout);
    elapsed = sceKernelGetProcessTimeWide() - start;
    notify_after = sceKernelNotifyCallback(callback, 1);
    return 0;
}

// A callback notified while its thread waits on a semaphore: the wait goes on after the callback
static void case_notified_sema(void) {
    const char *const name = "semaphore, notified during the wait, returns -1";
    runs = 0;
    wait_result = NOT_DONE;
    notify_after = NOT_DONE;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID thread = test_start_thread(sema_waiter);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA | WAIT_TYPE_CB, sema));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(runs > 0 && test_in_wait(thread, WAIT_TYPE_SEMA | WAIT_TYPE_CB, sema));
    sceKernelSignalSema(sema, 1);
    test_join_thread(thread);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(notify_after, "%s: notify after", name);
    sceKernelDeleteSema(sema);
}

// A callback pending when a semaphore wait with callbacks starts, which returns 1 and deletes
// itself
static void case_pending_sema(void) {
    const char *const name = "semaphore, pending at the start, returns 1";
    runs = 0;
    sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    callback = sceKernelCreateCallback("one", 0, returns_one, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = TEST_WAIT_US;
    const int result = sceKernelWaitSemaCB(sema, 1, &timeout);

    PRINTF(result, "%s: the wait", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(sceKernelNotifyCallback(callback, 1), "%s: notify after", name);
    PRINTF(sceKernelPollSema(sema, 1), "%s: poll after", name);
    sceKernelDeleteCallback(callback);
    sceKernelDeleteSema(sema);
}

// A callback that notifies itself and returns -1 on its second run, in a semaphore wait with a
// timeout of 0
static void case_final_sema(void) {
    const char *const name = "semaphore, timeout 0, self notify then -1";
    runs = 0;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    callback = sceKernelCreateCallback("self", 0, self_then_minus_one, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = 0;
    const int result = sceKernelWaitSemaCB(sema, 1, &timeout);

    PRINTF(result, "%s: the wait", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(sceKernelNotifyCallback(callback, 1), "%s: notify after", name);
    sceKernelDeleteCallback(callback);
    sceKernelDeleteSema(sema);
}

// A callback that notifies itself and returns -1 on its second run, in a mutex lock with timeout 0
static void case_final_mutex(void) {
    const char *const name = "mutex, timeout 0, self notify then -1";
    runs = 0;
    mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    release_sema = sceKernelCreateSema("release", 0, 0, 1, NULL);
    const SceUID holder = test_start_thread(holder_thread);
    TEST_WAIT_FOR(test_in_wait(holder, WAIT_TYPE_SEMA, release_sema));
    callback = sceKernelCreateCallback("self", 0, self_then_minus_one, NULL);
    sceKernelNotifyCallback(callback, 1);
    SceUInt timeout = 0;
    const int result = sceKernelLockMutexCB(mutex, 1, &timeout);
    const int runs_in_wait = runs;
    sceKernelSignalSema(release_sema, 1);
    test_join_thread(holder);

    PRINTF(result, "%s: the lock", name);
    PRINTF(runs_in_wait, "%s: runs of the callback", name);
    PRINTF(sceKernelNotifyCallback(callback, 1), "%s: notify after", name);
    sceKernelDeleteCallback(callback);
    sceKernelDeleteMutex(mutex);
    sceKernelDeleteSema(release_sema);
}

// A callback notified while its thread sleeps in a delay with callbacks
static void case_notified_delay(void) {
    const char *const name = "delay with callbacks, notified during the sleep, returns -1";
    runs = 0;
    wait_result = NOT_DONE;
    notify_after = NOT_DONE;
    elapsed = 0;
    const SceUID thread = test_start_thread(delay_waiter);
    TEST_WAIT_FOR(waits_with_callbacks(thread, WAIT_TYPE_DELAY));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(runs > 0 && waits_with_callbacks(thread, WAIT_TYPE_DELAY));
    test_join_thread(thread);

    PRINTF(wait_result, "%s: the delay", name);
    PRINTF(elapsed >= ENOUGH_US, "%s: it ran to its deadline", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(notify_after, "%s: notify after", name);
}

// A callback notified while its thread waits for a signal with callbacks
static void case_notified_signal(void) {
    const char *const name = "signal wait with callbacks, notified during the wait, returns -1";
    runs = 0;
    wait_result = NOT_DONE;
    notify_after = NOT_DONE;
    elapsed = 0;
    const SceUID thread = test_start_thread(signal_waiter);
    TEST_WAIT_FOR(waits_with_callbacks(thread, WAIT_TYPE_SIGNAL));
    sceKernelNotifyCallback(callback, 1);
    TEST_WAIT_FOR(runs > 0 && waits_with_callbacks(thread, WAIT_TYPE_SIGNAL));
    test_join_thread(thread);

    PRINTF(wait_result, "%s: the wait", name);
    PRINTF(elapsed >= ENOUGH_US, "%s: it ran to its deadline", name);
    PRINTF(runs, "%s: runs of the callback", name);
    PRINTF(notify_after, "%s: notify after", name);
}

int main(void) {
    case_notified_sema();
    case_pending_sema();
    case_final_sema();
    case_final_mutex();
    case_notified_delay();
    case_notified_signal();

    test_exit();
    return 0;
}
