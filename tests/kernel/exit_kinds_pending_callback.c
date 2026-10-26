// Three ways a thread ends with a notified callback still pending: returning from its function,
// sceKernelExitThread and sceKernelExitDeleteThread, each with its own status. Every thread makes
// its callback, waits on its own semaphore without callbacks, and the main thread notifies the
// callback while all three wait. Then each thread is signaled in turn and ends from its own
// function, never from a callback. The cases: the notify while it waits, the callback's runs after
// the end, the notify after the end, and the exit status of a thread that still exists.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum { BY_RETURN,
       BY_EXIT,
       BY_EXIT_DELETE,
       WAYS };

#define RETURN_STATUS 0x11
#define EXIT_STATUS 0x22
#define EXIT_DELETE_STATUS 0x33

static const char *const names[WAYS] = { "return", "exit", "exit and delete" };
static SceUID go[WAYS], cb[WAYS];
static volatile int calls[WAYS];

// Counts the runs of the callback of each way, which is passed as common
static int counting_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    calls[(int)(SceUIntPtr)common]++;
    return 0;
}

// Makes its callback, waits on its semaphore without callbacks, then ends the way its argument says
static int ending_thread(SceSize args, void *argp) {
    (void)args;
    const int way = *(const int *)argp;
    cb[way] = sceKernelCreateCallback("ending", 0, counting_callback, (void *)(SceUIntPtr)way);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(go[way], 1, &timeout);
    switch (way) {
    case BY_EXIT:
        sceKernelExitThread(EXIT_STATUS);
        break;
    case BY_EXIT_DELETE:
        sceKernelExitDeleteThread(EXIT_DELETE_STATUS);
        break;
    }
    return RETURN_STATUS;
}

static int thread_gone(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) < 0;
}

int main(void) {
    SceUID thread[WAYS];
    for (int way = 0; way < WAYS; way++) {
        go[way] = sceKernelCreateSema("go", 0, 0, 1, NULL);
        thread[way] = test_start_thread_arg(ending_thread, way);
    }
    for (int way = 0; way < WAYS; way++) {
        test_wait_until_waiting(thread[way]);
        PRINTF(sceKernelNotifyCallback(cb[way], 1), "%s: notify the callback while it waits", names[way]);
    }

    for (int way = 0; way < WAYS; way++) {
        sceKernelSignalSema(go[way], 1);
        if (way == BY_EXIT_DELETE) {
            TEST_WAIT_FOR(thread_gone(thread[way]));
            PRINTF(thread_gone(thread[way]), "%s: the thread is gone", names[way]);
        } else {
            SceUInt timeout = TEST_WAIT_US;
            int status = 0xDEAD;
            PRINTF(sceKernelWaitThreadEnd(thread[way], NULL, &timeout), "%s: the end wait", names[way]);
            PRINTF(sceKernelGetThreadExitStatus(thread[way], &status), "%s: the exit status call", names[way]);
            PRINTF(status, "%s: the exit status", names[way]);
            sceKernelDeleteThread(thread[way]);
        }
        PRINTF(calls[way], "%s: the callback's runs", names[way]);
        PRINTF(sceKernelNotifyCallback(cb[way], 1), "%s: notify the callback after the end", names[way]);
    }

    test_exit();
    return 0;
}
