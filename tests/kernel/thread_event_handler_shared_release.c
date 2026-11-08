// One START handler registered for every thread runs on two threads at once, and one of the runs
// returns nonzero, which asks for the handler to be unregistered. Thread A's run waits on a gate.
// While it waits, thread B starts, its run of the same handler returns 1 and B ends. The main
// thread then checks whether the handler's UID still has its class, lets A's run go on, and A's
// run tries to unregister the handler from inside the handler before it returns 0. After A ends,
// the main thread checks the UID's class again and unregisters it. Every wait has a timeout.
//
// On a PS Vita TV on firmware 3.65 the handler's UID keeps its class while A's run waits, A's
// unregister from inside the handler gives 0x80027101, and after A's run the UID still has its
// class while an unregister from the main thread gives 0x80028061.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID handler_id, gate, thread_a, thread_b;
static volatile int a_in_handler, a_unregister = 0xDEAD, b_runs;

static int start_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)arg;
    (void)common;
    if (thread_id == thread_b) {
        b_runs++;
        return 1;
    }
    if (thread_id != thread_a)
        return 0;
    a_in_handler = 1;
    SceUInt timeout = 10 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &timeout);
    a_unregister = sceKernelUnregisterThreadEventHandler(handler_id);
    return 0;
}

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

int main(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    thread_a = sceKernelCreateThread("A", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    thread_b = sceKernelCreateThread("B", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    handler_id = sceKernelRegisterThreadEventHandler("shared", SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID, SCE_KERNEL_THREAD_EVENT_TYPE_START, start_handler, NULL);
    PRINT("register the handler", test_created(handler_id));
    const SceInt32 handler_class = get_uid_class(handler_id);

    sceKernelStartThread(thread_a, 0, NULL);
    TEST_WAIT_FOR(a_in_handler);
    const int a_waiting = a_in_handler;
    sceKernelStartThread(thread_b, 0, NULL);
    SceUInt timeout = 10 * TEST_WAIT_US;
    const int b_end = sceKernelWaitThreadEnd(thread_b, NULL, &timeout);
    const int known_during = get_uid_class(handler_id) == handler_class;

    sceKernelSignalSema(gate, 1);
    timeout = 10 * TEST_WAIT_US;
    const int a_end = sceKernelWaitThreadEnd(thread_a, NULL, &timeout);
    const int known_after = get_uid_class(handler_id) == handler_class;
    const int unregister_after = sceKernelUnregisterThreadEventHandler(handler_id);

    PRINT("A's run waits in the handler", a_waiting);
    PRINT("B's end", b_end);
    PRINT("runs of the handler on B", b_runs);
    PRINT("the handler's UID has its class while A's run waits", known_during);
    PRINT("A's unregister from inside the handler", a_unregister);
    PRINT("A's end", a_end);
    PRINT("the handler's UID has its class after A's run", known_after);
    PRINT("unregister it from the main thread after", unregister_after);

    sceKernelDeleteThread(thread_a);
    sceKernelDeleteThread(thread_b);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
