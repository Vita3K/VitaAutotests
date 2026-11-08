// Whether a START handler starts on the same sp as its thread's entry, and an END handler too. The
// handlers and the entry are naked, so each records the sp it was called with before anything
// moves it. The entry takes a 40 byte argument. The thread runs once, then is started again with
// both handlers still registered. Only comparisons are printed, since stack addresses differ
// between runs.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ARG_SIZE 40

static unsigned char arg_bytes[ARG_SIZE];
static volatile unsigned int entry_sp, start_sp, end_sp;
static volatile int entry_runs, start_runs, end_runs;

// Called by the naked functions with their sp after their push of two words
void record_entry_sp(unsigned int pushed);
void record_entry_sp(unsigned int pushed) {
    entry_runs++;
    entry_sp = pushed + 8;
}

void record_start_sp(unsigned int pushed);
void record_start_sp(unsigned int pushed) {
    start_runs++;
    start_sp = pushed + 8;
}

void record_end_sp(unsigned int pushed);
void record_end_sp(unsigned int pushed) {
    end_runs++;
    end_sp = pushed + 8;
}

__attribute__((naked)) static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_entry_sp\n\t"
        "pop {r4, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

__attribute__((naked)) static int start_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_start_sp\n\t"
        "pop {r4, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

__attribute__((naked)) static int end_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_end_sp\n\t"
        "pop {r4, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// Starts thread, waits for its end, and prints what the run recorded
static void run(const char *name, SceUID thread) {
    entry_sp = start_sp = end_sp = 0;
    entry_runs = start_runs = end_runs = 0;
    const int started = sceKernelStartThread(thread, ARG_SIZE, arg_bytes);
    SceUInt timeout = TEST_WAIT_US;
    const int ended = sceKernelWaitThreadEnd(thread, NULL, &timeout);
    PRINTF(started, "%s: start", name);
    PRINTF(ended, "%s: wait for the end", name);
    PRINTF(entry_runs, "%s: entry runs", name);
    PRINTF(start_runs, "%s: START handler runs", name);
    PRINTF(end_runs, "%s: END handler runs", name);
    PRINTF(start_sp == entry_sp, "%s: the START handler's sp equals the entry's sp", name);
    PRINTF(end_sp == entry_sp, "%s: the END handler's sp equals the entry's sp", name);
}

int main(void) {
    for (int i = 0; i < ARG_SIZE; i++)
        arg_bytes[i] = (unsigned char)(0xA0 + i);

    const SceUID thread = sceKernelCreateThread("target", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    PRINT("create the thread", test_created(thread));
    const SceUID start = sceKernelRegisterThreadEventHandler("start sp", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START, start_hook, NULL);
    PRINT("register the START handler", test_created(start));
    const SceUID end = sceKernelRegisterThreadEventHandler("end sp", thread, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_hook, NULL);
    PRINT("register the END handler", test_created(end));

    run("a fresh thread", thread);
    run("started again", thread);

    PRINT("unregister the START handler", sceKernelUnregisterThreadEventHandler(start));
    PRINT("unregister the END handler", sceKernelUnregisterThreadEventHandler(end));
    PRINT("delete the thread", sceKernelDeleteThread(thread));
    test_exit();
    return 0;
}
