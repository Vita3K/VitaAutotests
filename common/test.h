// Shared helpers for the tests. A test prints what it observes, which should match its .expected
// file.
#pragma once

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

// Every output line also goes to ux0:data/vita-autotests/output/<test name>.txt, since a Vita's
// debug output can drop lines in a burst. main.c sets the path before it runs the test.
#define TEST_OUTPUT_DIR "ux0:data/vita-autotests/output"
extern char test_output_path[128];

// Writes one line of the test's output to the debug output and to test_output_path
static inline void test_output(const char *line, int len) {
    static SceUID fd = -1;
    sceClibPrintf("%s", line);
    if (fd < 0)
        fd = sceIoOpen(test_output_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0)
        sceIoWrite(fd, line, len);
    // Gives whatever reads the debug output time to keep up
    sceKernelDelayThread(2000);
}

// Prints one observed value as a line of the test's output
#define PRINT(name, value) test_print(name, (int)(value))

static inline void test_print(const char *name, int value) {
    char line[160];
    const int len = sceClibSnprintf(line, sizeof(line), "%s: 0x%08X\n", name, (unsigned)value);
    test_output(line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1);
}

// Returns 0 for the UID of a created object, or the error, so the output doesn't depend on UID values
static inline int test_created(SceUID uid) {
    return uid < 0 ? uid : 0;
}

// Starts entry on a new thread, passing it a copy of arg
static inline SceUID test_start_thread_arg(SceKernelThreadEntry entry, int arg) {
    const SceUID thread = sceKernelCreateThread("test", entry, 0x10000100, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(arg), &arg);
    return thread;
}

// Starts entry on a new thread
static inline SceUID test_start_thread(SceKernelThreadEntry entry) {
    return test_start_thread_arg(entry, 0);
}

// Waits for a thread from test_start_thread to end and deletes it
static inline void test_join_thread(SceUID thread) {
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);
}

// How long a waiter thread waits before giving up. Long, since starting the other threads of a test
// can take a while, and a waiter must still be waiting when the test wakes it.
#define TEST_WAIT_US 2000000

// Gives started threads time to reach their waits
static inline void test_let_threads_wait(void) {
    sceKernelDelayThread(10000);
}

// Waits until thread is in a wait, for at most half a second, so waiters queue in a known order
static inline void test_wait_until_waiting(SceUID thread) {
    for (int i = 0; i < 500; i++) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        if (sceKernelGetThreadInfo(thread, &info) < 0 || info.waitType != 0)
            return;
        sceKernelDelayThread(1000);
    }
}

// Ends the test. The last output line marks that it got this far.
static inline void test_exit(void) {
    test_output("DONE\n", 5);
    sceKernelExitProcess(0);
}
