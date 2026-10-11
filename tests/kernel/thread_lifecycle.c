// A thread's life: how its start arguments arrive, how it ends and what its exit status is, and what
// works on it after it ends
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static char sent[0x8000];
static int arg_size, arg_copied, arg_matches, arg_null, own_id_matches, runs;
static SceUID expected_id;

static int returns_7(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    runs++;
    return 7;
}

static int exits_9(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitThread(9);
    return 1;
}

static int exit_deletes(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelExitDeleteThread(11);
    return 1;
}

static int checks_args(SceSize args, void *argp) {
    arg_size = args;
    arg_null = argp == NULL;
    arg_copied = argp && argp != sent;
    arg_matches = argp && sceClibMemcmp(argp, sent, args < sizeof(sent) ? args : sizeof(sent)) == 0;
    own_id_matches = sceKernelGetThreadId() == expected_id;
    return 0;
}

// Starts entry, waits for its end and returns the status it reports, or the error
static int end_status(SceKernelThreadEntry entry, SceUID *thread) {
    *thread = sceKernelCreateThread("t", entry, 0x10000100, 0x4000, 0, 0, NULL);
    sceKernelStartThread(*thread, 0, NULL);
    int status = 0xDEAD;
    SceUInt timeout = TEST_WAIT_US;
    const int ret = sceKernelWaitThreadEnd(*thread, &status, &timeout);
    return ret < 0 ? ret : status;
}

int main(void) {
    SceUID thread;

    // Ending
    PRINT("the status of a thread that returns 7", end_status(returns_7, &thread));
    int status = 0xDEAD;
    PRINT("its exit status", sceKernelGetThreadExitStatus(thread, &status));
    PRINT("the status", status);
    PRINT("start it again", sceKernelStartThread(thread, 0, NULL));
    SceUInt timeout = TEST_WAIT_US;
    PRINT("wait for its second end", sceKernelWaitThreadEnd(thread, &status, &timeout));
    PRINT("it ran twice", runs);
    PRINT("delete it", sceKernelDeleteThread(thread));
    PRINT("the status of a thread that exits with 9", end_status(exits_9, &thread));
    sceKernelDeleteThread(thread);
    PRINT("the status of a thread that exits and deletes itself", end_status(exit_deletes, &thread));
    timeout = 1000;
    PRINT("wait for it again", sceKernelWaitThreadEnd(thread, &status, &timeout));
    SceKernelThreadInfo info = { .size = sizeof(info) };
    PRINT("its info", sceKernelGetThreadInfo(thread, &info));
    PRINT("delete it", sceKernelDeleteThread(thread));

    // Start arguments
    for (unsigned int i = 0; i < sizeof(sent); i++)
        sent[i] = (char)i;
    static const SceSize sizes[] = { 0, 1, 4, 13, 0x100, 0x800 };
    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        thread = sceKernelCreateThread("args", checks_args, 0x10000100, 0x4000, 0, 0, NULL);
        expected_id = thread;
        arg_size = arg_copied = arg_matches = arg_null = own_id_matches = 0xDEAD;
        PRINTF(sceKernelStartThread(thread, sizes[i], sent), "start with 0x%X bytes of arguments", sizes[i]);
        test_join_thread(thread);
        PRINTF(arg_size, "0x%X bytes: the size it gets", sizes[i]);
        PRINTF(arg_null, "0x%X bytes: it gets a NULL pointer", sizes[i]);
        PRINTF(arg_copied, "0x%X bytes: it gets a copy", sizes[i]);
        PRINTF(arg_matches, "0x%X bytes: the copy matches", sizes[i]);
        PRINTF(own_id_matches, "0x%X bytes: its thread ID is the created one", sizes[i]);
    }
    thread = sceKernelCreateThread("args", checks_args, 0x10000100, 0x4000, 0, 0, NULL);
    PRINT("start with more arguments than the stack holds", sceKernelStartThread(thread, 0x8000, sent));
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);

    test_exit();
    return 0;
}
