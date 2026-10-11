// Thread local storage slots: whether each thread gets its own, what a new slot holds, which keys
// exist, and reading another thread's slots
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUInt32 *main_slot;
static SceUID release;
static int thread_slot_differs, thread_slot_is_zero, thread_sees_own_write;
static SceUInt32 *thread_slot;

// Looks at slot 0 from a new thread, then waits until release is signaled
static int slot_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    thread_slot = sceKernelGetTLSAddr(0);
    thread_slot_differs = thread_slot != main_slot;
    thread_slot_is_zero = thread_slot && *thread_slot == 0;
    if (thread_slot)
        *thread_slot = 0x5678;
    thread_sees_own_write = thread_slot && *(SceUInt32 *)sceKernelGetTLSAddr(0) == 0x5678;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    return 0;
}

int main(void) {
    main_slot = sceKernelGetTLSAddr(0);
    PRINT("slot 0 has an address", main_slot != NULL);
    PRINT("it is 4 byte aligned", ((SceUIntPtr)main_slot & 3) == 0);
    PRINT("the same key gives the same address", sceKernelGetTLSAddr(0) == main_slot);
    PRINT("key 1 is the next word", (SceUInt32 *)sceKernelGetTLSAddr(1) == main_slot + 1);
    *main_slot = 0x1234;

    // Which keys exist
    static const int keys[] = { 15, 16, 63, 64, 255, 256, -1 };
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        PRINTF(sceKernelGetTLSAddr(keys[i]) != NULL, "key %d has an address", keys[i]);

    // Another thread
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    const SceUID thread = test_start_thread(slot_thread);
    test_wait_until_waiting(thread);
    PRINT("a new thread's slot 0 has another address", thread_slot_differs);
    PRINT("it starts as 0", thread_slot_is_zero);
    PRINT("the thread reads back its own write", thread_sees_own_write);
    PRINT("this thread's slot keeps its value", *main_slot == 0x1234);
    PRINT("the other thread's slot by its UID is its slot", sceKernelGetThreadTLSAddr(thread, 0) == thread_slot);
    PRINT("this thread's slot by its UID", sceKernelGetThreadTLSAddr(sceKernelGetThreadId(), 0) == main_slot);
    PRINT("this thread's slot by UID 0", sceKernelGetThreadTLSAddr(0, 0) == main_slot);
    PRINT("a slot of an unknown thread", sceKernelGetThreadTLSAddr(0x12345, 0) != NULL);
    sceKernelSignalSema(release, 1);
    test_join_thread(thread);
    PRINT("a slot of the ended and deleted thread", sceKernelGetThreadTLSAddr(thread, 0) != NULL);

    test_exit();
    return 0;
}
