// What a notify of a deleted callback does while its owner T waits with callbacks inside that
// callback's run. T makes a callback, notifies it and runs it with a check. On its first run the
// callback deletes itself, then waits on a semaphore at 0 with callbacks. A deleted callback keeps
// its UID until its run ends, as callback_delete_in_run shows. Both threads are pinned to core 0.
// The main thread runs at 0x60 and T at 0xB0, so T stays ready until the main thread blocks. The
// main thread waits until T waits on the semaphore, notifies the callback, and right away, with no
// call that can block in between, reads T's thread info, the semaphore's waiting threads and the
// callback's count and notify ID. Then it blocks until T waits on the semaphore again, and reads the
// same values and the callback's runs. Then it signals the semaphore, joins T and prints. Inside the
// run, after the wait, the callback reads its count. After the run T reads the count, notifies and
// deletes the callback, and runs a second check. The prints come last, since a print blocks the main
// thread and lets T run. The waits have timeouts of a few seconds, and the main thread signals the
// semaphore whatever it saw, so T always ends.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x60
#define WAITER_PRIORITY 0xB0
#define NOT_DONE 0xDEAD
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define WAIT_TYPE (WAIT_TYPE_SEMA | WAIT_TYPE_CB)

// What the main thread reads while T is ready or waits
typedef struct State {
    int status;
    SceUInt32 wait_type;
    int notify_flag;
    int waiters;
    int count;
    SceUID notify_id;
} State;

static SceUID sema, cb;
static volatile int runs, delete_inside, nested, count_inside, check, count_after, notify_after,
    delete_after, next_check;

static int deleted_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    if (runs > 1)
        return 0;
    delete_inside = sceKernelDeleteCallback(cb);
    SceUInt timeout = LONG_TIMEOUT;
    nested = sceKernelWaitSemaCB(sema, 1, &timeout);
    count_inside = sceKernelGetCallbackCount(cb);
    return 0;
}

// Makes the callback, runs it with a check, then calls on its UID after the run
static int owner(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb = sceKernelCreateCallback("deleted in run", 0, deleted_callback, NULL);
    sceKernelNotifyCallback(cb, 1);
    check = sceKernelCheckCallback();
    count_after = sceKernelGetCallbackCount(cb);
    notify_after = sceKernelNotifyCallback(cb, 2);
    delete_after = sceKernelDeleteCallback(cb);
    next_check = sceKernelCheckCallback();
    return 0;
}

// Reads T's thread info, the semaphore's info and the callback's info, with no call that can block
static State read_state(SceUID thread) {
    State s = { NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE };
    SceKernelThreadInfo thread_info = { .size = sizeof(thread_info) };
    if (sceKernelGetThreadInfo(thread, &thread_info) == 0) {
        s.status = thread_info.status;
        s.wait_type = thread_info.waitType;
        s.notify_flag = thread_info.fNotifyCallback;
    }
    SceKernelSemaInfo sema_info = { .size = sizeof(sema_info) };
    if (sceKernelGetSemaInfo(sema, &sema_info) == 0)
        s.waiters = sema_info.numWaitThreads;
    s.count = sceKernelGetCallbackCount(cb);
    CallbackInfo cb_info = { .size = sizeof(cb_info) };
    if (sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&cb_info) == 0)
        s.notify_id = cb_info.notifyId;
    return s;
}

static void print_state(const char *when, const State *s) {
    PRINTF(s->status, "T's status %s", when);
    PRINTF(s->wait_type, "T's wait type %s", when);
    PRINTF(s->notify_flag, "T's notify flag %s", when);
    PRINTF(s->waiters, "the semaphore's waiting threads %s", when);
    PRINTF(s->count, "the notifications %s", when);
    PRINTF(s->notify_id, "the notify ID %s", when);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    runs = 0;
    delete_inside = nested = count_inside = check = count_after = notify_after = delete_after = next_check
        = NOT_DONE;
    sema = sceKernelCreateSema("nested wait", 0, 0, 1, NULL);
    const SceUID thread
        = sceKernelCreateThread("t", owner, WAITER_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE, sema));
    const int waits = test_in_wait(thread, WAIT_TYPE, sema);

    const int notify = sceKernelNotifyCallback(cb, 0x55);
    const State after = read_state(thread);

    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE, sema));
    const int waits_again = test_in_wait(thread, WAIT_TYPE, sema);
    const State again = read_state(thread);
    const int runs_then = runs;

    const int signal = sceKernelSignalSema(sema, 1);
    test_join_thread(thread);

    PRINT("T waits in the nested wait before the notify", waits);
    PRINT("the notify of the deleted callback", notify);
    print_state("after the notify", &after);
    PRINT("T waits in the nested wait again", waits_again);
    print_state("then", &again);
    PRINT("runs of the callback then", runs_then);
    PRINT("the signal", signal);
    PRINT("the nested wait", nested);
    PRINT("the delete inside the run", delete_inside);
    PRINT("the notifications after the nested wait, inside the run", count_inside);
    PRINT("T's check", check);
    PRINT("runs", runs);
    PRINT("after the run: the notifications", count_after);
    PRINT("after the run: the notify", notify_after);
    PRINT("after the run: the delete", delete_after);
    PRINT("T's next check", next_check);
    sceKernelDeleteSema(sema);

    test_exit();
    return 0;
}
