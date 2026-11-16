// TLS slots 6 and 7: whether slot 6 differs between two threads, whether slot 7 is the same in the
// main thread and two new threads, and whether libkernel's two stack canary words, __stack_chk_guard
// and SceKernelStackChkGuard, hold the main thread's slot 7. Slot 6 is random, and two random
// words match about once in 2^32 runs, which the test accepts.
//
// On a PS Vita TV on firmware 3.65 slot 6 differs between the two new threads and between the main
// thread and a new thread. Slot 7 is the same nonzero word in all three threads, and both canary
// words hold it.
#include <psp2/kernel/threadmgr.h>

#include "extra_imports.h"
#include "test.h"

static SceUInt32 slot6[2], slot7[2];

// Addresses in data, so the imports get word relocations, which the loaders support for variables
static volatile SceUInt32 *volatile libkernel_guard = &libkernel_stack_chk_guard;
static volatile SceUInt32 *volatile kernel_guard = &SceKernelStackChkGuard;

static int entry(SceSize args, void *argp) {
    (void)args;
    const int index = *(int *)argp;
    slot6[index] = *(volatile SceUInt32 *)sceKernelGetTLSAddr(6);
    slot7[index] = *(volatile SceUInt32 *)sceKernelGetTLSAddr(7);
    return 0;
}

int main(void) {
    const SceUInt32 main_slot6 = *(volatile SceUInt32 *)sceKernelGetTLSAddr(6);
    const SceUInt32 main_slot7 = *(volatile SceUInt32 *)sceKernelGetTLSAddr(7);
    for (int i = 0; i < 2; i++)
        test_join_thread(test_start_thread_arg(entry, i));

    PRINT("slot 6 is the same in the two new threads", slot6[0] == slot6[1]);
    PRINT("slot 6 is the same in the main thread and a new thread", main_slot6 == slot6[0]);
    PRINT("slot 7 of the first new thread is the main thread's", slot7[0] == main_slot7);
    PRINT("slot 7 of the second new thread is the main thread's", slot7[1] == main_slot7);
    PRINT("slot 7 isn't 0", main_slot7 != 0);
    PRINT("__stack_chk_guard is slot 7", *libkernel_guard == main_slot7);
    PRINT("SceKernelStackChkGuard is slot 7", *kernel_guard == main_slot7);

    test_exit();
    return 0;
}
