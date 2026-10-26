// A thread's children after the thread that made them has ended or been deleted. A creator at
// priority 0x70 makes a dormant child with priority 0, then lowers itself to 0x60, and then either
// returns or exit-deletes itself. The child should keep the priority it had when it was made, and
// work the same as one whose creator is still alive. Four cases: the creator returns or
// exit-deletes, and the child is either started by main after the creator is gone, or started by
// the creator before it ends, and then waits on a semaphore while the creator is gone. Each case
// prints the child's initial priority and whether its process is this process, the results of its
// start, end wait and delete, and the priority and count of its runs. The child's waits are checked
// while it is blocked, and it is only read after it has ended or been blocked.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// What a creator does, as bits of its argument
#define MODE_START 0x1
#define MODE_EXIT_DELETE 0x2

// The priority the creator runs at when it makes the child
#define CREATOR_PRIORITY 0x70
// The priority the creator lowers itself to after it has made the child
#define LOWERED_PRIORITY 0x60

// How long the child waits on the semaphore, a wide margin over the steps that main takes
#define CHILD_WAIT_US 5000000

static SceUID child, hold;
static volatile int child_runs, child_priority, child_wait_result;
// Whether the child waits on hold after it records its priority, which the case decides
static int child_waits;

// Records the priority it runs at, then waits on hold if the case asks for it
static int child_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    child_priority = sceKernelGetThreadCurrentPriority();
    child_runs++;
    if (child_waits) {
        SceUInt timeout = CHILD_WAIT_US;
        child_wait_result = sceKernelWaitSema(hold, 1, &timeout);
    }
    return 0;
}

// Makes a dormant child with priority 0, lowers itself, and starts the child when the mode says so.
// Then it returns, or exit-deletes itself when the mode says so.
static int creator_entry(SceSize args, void *argp) {
    (void)args;
    const int mode = *(const int *)argp;
    child = sceKernelCreateThread("child", child_entry, 0, 0x4000, 0, 0, NULL);
    sceKernelChangeThreadPriority(0, LOWERED_PRIORITY);
    if (mode & MODE_START)
        sceKernelStartThread(child, 0, NULL);
    if (mode & MODE_EXIT_DELETE)
        sceKernelExitDeleteThread(0);
    return 0;
}

// Starts a creator at CREATOR_PRIORITY with mode, and returns its UID
static SceUID start_creator(int mode) {
    const SceUID creator = sceKernelCreateThread("creator", creator_entry, CREATOR_PRIORITY, 0x4000, 0, 0, NULL);
    sceKernelStartThread(creator, sizeof(mode), &mode);
    return creator;
}

// Returns whether thread is gone, which it is once it has been deleted
static int thread_gone(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) < 0;
}

// Waits for a creator that returned to end, then deletes it, and prints the wait result
static void join_creator(const char *name, SceUID creator) {
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(creator, NULL, &timeout), "%s: the creator's end wait", name);
    sceKernelDeleteThread(creator);
}

// Prints the child's initial priority and whether it belongs to this process
static void print_child_info(const char *name) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(child, &info);
    PRINTF(info.initPriority, "%s: the child's initial priority", name);
    PRINTF(info.processId == sceKernelGetProcessId(), "%s: the child is in this process", name);
}

// Starts the child from main, waits for its end and deletes it, printing what happened
static void start_child_from_main(const char *name) {
    print_child_info(name);
    PRINTF(sceKernelStartThread(child, 0, NULL), "%s: start the child", name);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(child, NULL, &timeout), "%s: the child's end wait", name);
    PRINTF(child_priority, "%s: the priority the child runs at", name);
    PRINTF(child_runs, "%s: runs of the child", name);
    PRINTF(sceKernelDeleteThread(child), "%s: delete the child", name);
}

// Waits until the child waits on hold, then lets it end and deletes it, printing what happened
static void finish_waiting_child(const char *name) {
    TEST_WAIT_FOR(test_in_wait(child, WAIT_TYPE_SEMA, hold));
    PRINTF(test_in_wait(child, WAIT_TYPE_SEMA, hold), "%s: the child waits on the semaphore", name);
    print_child_info(name);
    PRINTF(child_priority, "%s: the priority the child runs at", name);
    PRINTF(sceKernelSignalSema(hold, 1), "%s: signal the child", name);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(child, NULL, &timeout), "%s: the child's end wait", name);
    PRINTF(child_wait_result, "%s: the child's wait result", name);
    PRINTF(child_runs, "%s: runs of the child", name);
    PRINTF(sceKernelDeleteThread(child), "%s: delete the child", name);
}

// Resets what the children record, before a case
static void reset_child(void) {
    child_runs = 0;
    child_priority = -1;
    child_wait_result = 0xDEAD;
}

int main(void) {
    hold = sceKernelCreateSema("hold", 0, 0, 1, NULL);

    // The creator returns, and main starts the child afterwards
    reset_child();
    child_waits = 0;
    join_creator("returned creator, child started later", start_creator(0));
    start_child_from_main("returned creator, child started later");

    // The creator exit-deletes itself, and main starts the child afterwards
    reset_child();
    child_waits = 0;
    const SceUID exiting = start_creator(MODE_EXIT_DELETE);
    TEST_WAIT_FOR(thread_gone(exiting));
    PRINT("exit-deleted creator, child started later: the creator is gone", thread_gone(exiting));
    start_child_from_main("exit-deleted creator, child started later");

    // The creator starts the child, then returns while the child waits on hold
    reset_child();
    child_waits = 1;
    join_creator("returned creator, child waiting", start_creator(MODE_START));
    finish_waiting_child("returned creator, child waiting");

    // The creator starts the child, then exit-deletes itself while the child waits on hold
    reset_child();
    child_waits = 1;
    const SceUID exiting_waiter = start_creator(MODE_START | MODE_EXIT_DELETE);
    TEST_WAIT_FOR(thread_gone(exiting_waiter));
    PRINT("exit-deleted creator, child waiting: the creator is gone", thread_gone(exiting_waiter));
    finish_waiting_child("exit-deleted creator, child waiting");

    sceKernelDeleteSema(hold);
    test_exit();
    return 0;
}
