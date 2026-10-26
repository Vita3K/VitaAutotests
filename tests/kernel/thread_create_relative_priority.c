// Priorities that the create and change calls take relative to the process's default priority,
// not to the caller's. The main thread sets its own priority to 0x50, 0xA0 and 0xB0 in turn. At
// each one it creates a dormant thread with each of seven values: the default 0x10000100, the two
// ends of the relative range 0x100000E0 to 0x1000011F, the values just outside that range, a plain
// 0x70, and 0, which stands for the caller's own priority. For each create it prints the result,
// the thread's initial and current priority, and the priority the thread runs at after it starts
// and ends. At 0x50 it also changes a waiting thread W, at 0x90, to each value with
// ChangeThreadPriority and then with ChangeThreadPriority2, and prints each result and W's
// priority after it. W is set back to 0x90 after each pair of calls. Main's priority is set back
// to its starting value at the end.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define W_PRIORITY 0x90

int sceKernelChangeThreadPriority2(SceUID thid, int priority);

static const int callers[] = { 0x50, 0xA0, 0xB0 };
static const int values[] = {
    0x10000100, 0x100000E0, 0x1000011F, 0x100000DF, 0x10000120, 0x70, 0,
};

static SceUID sema;
static volatile int run_priority;

static int run_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    run_priority = sceKernelGetThreadCurrentPriority();
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 5 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

static int current_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info.currentPriority;
}

// Waits for thread to end, for at most 5 seconds, and deletes it
static void join(SceUID thread) {
    SceUInt32 timeout = 5 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

// Creates a dormant thread at priority, with the caller at caller, then starts it and prints what
// it reports before the start and the priority it runs at
static void create_and_run(int caller, int priority) {
    const SceUID thread =
        sceKernelCreateThread("relative", run_entry, priority, 0x4000, 0, 0, NULL);
    PRINTF(test_created(thread), "create with 0x%X from 0x%X", priority, caller);
    if (thread < 0)
        return;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    PRINTF(info.initPriority, "initial priority of 0x%X from 0x%X", priority, caller);
    PRINTF(info.currentPriority, "current priority of 0x%X from 0x%X", priority, caller);
    run_priority = -1;
    sceKernelStartThread(thread, 0, NULL);
    join(thread);
    PRINTF(run_priority, "the priority it runs at for 0x%X from 0x%X", priority, caller);
}

// Changes the waiting thread w to each value, with each of the two change calls, and prints the
// result of each call and w's priority after it
static void change_waiting(SceUID w, int caller) {
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        const int p = values[i];
        PRINTF(sceKernelChangeThreadPriority(w, p), "change W to 0x%X from 0x%X", p, caller);
        PRINTF(current_of(w), "W's priority after that change to 0x%X from 0x%X", p, caller);
        sceKernelChangeThreadPriority(w, W_PRIORITY);
        PRINTF(sceKernelChangeThreadPriority2(w, p),
               "change W to 0x%X with the second call from 0x%X", p, caller);
        PRINTF(current_of(w), "W's priority after the second call with 0x%X from 0x%X", p, caller);
        sceKernelChangeThreadPriority(w, W_PRIORITY);
    }
}

int main(void) {
    const int start_priority = sceKernelGetThreadCurrentPriority();
    PRINT("the main thread's priority", start_priority);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID w = sceKernelCreateThread("w", w_entry, W_PRIORITY, 0x4000, 0, 0, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(w, WAIT_TYPE_SEMA, sema));

    for (unsigned int c = 0; c < sizeof(callers) / sizeof(callers[0]); c++) {
        const int caller = callers[c];
        PRINTF(sceKernelChangeThreadPriority(0, caller), "main changes itself to 0x%X", caller);
        for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); i++)
            create_and_run(caller, values[i]);
        if (caller == 0x50)
            change_waiting(w, caller);
    }

    sceKernelChangeThreadPriority(0, start_priority);
    PRINT("the main thread's priority at the end", sceKernelGetThreadCurrentPriority());
    sceKernelSignalSema(sema, 1);
    join(w);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
