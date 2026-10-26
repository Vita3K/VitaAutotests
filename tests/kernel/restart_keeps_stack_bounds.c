// The stack of a thread, and TLS slots 2 and 3 that describe it, in its first run and in a run
// after a restart. At the start of each run the thread records the low end and size of its stack
// from its info, and the values of TLS slots 2 and 3, then ends. The main thread restarts it once
// it has ended. The test prints whether the stack and its size are the same in both runs, and for
// each run whether slot 3 is the low end of the stack and slot 2 minus slot 3 is the size. Last,
// whether slots 2 and 3 hold the same values in both runs.
// Models thread_restart and thread_stack_free, and tls_start_slots, which gives the kinds of slots
// 2 and 3 for a new thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define STACK_SIZE 0x4000

typedef struct RunRecord {
    SceUIntPtr stack;
    SceSize size;
    SceUInt32 slot2, slot3;
} RunRecord;

static RunRecord records[2];
static volatile int runs;

static int thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    RunRecord *record = &records[runs++];
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    record->stack = (SceUIntPtr)info.stack;
    record->size = info.stackSize;
    record->slot2 = *(volatile SceUInt32 *)sceKernelGetTLSAddr(2);
    record->slot3 = *(volatile SceUInt32 *)sceKernelGetTLSAddr(3);
    return 0;
}

int main(void) {
    const SceUID thread = sceKernelCreateThread("stack", thread_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, STACK_SIZE, 0, 0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);

    sceKernelStartThread(thread, 0, NULL);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    PRINT("delete it", sceKernelDeleteThread(thread));

    PRINT("the stack is the same in both runs", records[0].stack == records[1].stack);
    PRINT("the stack size is the same in both runs", records[0].size == records[1].size);
    for (int run = 0; run < 2; run++) {
        const RunRecord *record = &records[run];
        PRINTF(record->slot3 == (SceUInt32)record->stack, "run %d: slot 3 is the low end of the stack", run + 1);
        PRINTF(record->slot2 - record->slot3 == record->size, "run %d: slot 2 minus slot 3 is the stack size", run + 1);
    }
    PRINT("slot 2 holds the same value in both runs", records[0].slot2 == records[1].slot2);
    PRINT("slot 3 holds the same value in both runs", records[0].slot3 == records[1].slot3);

    test_exit();
    return 0;
}
