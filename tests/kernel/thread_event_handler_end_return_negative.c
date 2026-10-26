// An END handler that returns -1, and a control END handler that returns 0, each registered for a
// thread that is started twice. Per case the test prints the result of each wait for the end, the
// runs of the handler after each start, and the result of unregistering the handler at the end.
// The return value of the handler is the only difference between the two cases.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static SceUID target;
static volatile int end_runs, return_value;

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Counts its END runs and returns return_value
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    end_runs++;
    return return_value;
}

static void run_case(const char *name, int returns) {
    end_runs = 0;
    return_value = returns;
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINTF(id < 0 ? id : 1, "%s: register", name);
    for (int start = 1; start <= 2; start++) {
        sceKernelStartThread(target, 0, NULL);
        SceUInt timeout = TEST_WAIT_US;
        PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: start %d: the wait for the end", name, start);
        PRINTF(end_runs, "%s: start %d: runs of the handler", name, start);
    }
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "%s: unregister it after", name);
    sceKernelDeleteThread(target);
}

int main(void) {
    run_case("an end handler that returns -1", -1);
    run_case("a control end handler that returns 0", 0);

    test_exit();
    return 0;
}
