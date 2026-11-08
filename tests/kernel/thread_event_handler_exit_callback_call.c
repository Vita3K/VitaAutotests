// What _sceKernelExitCallback does when an END handler calls it instead of returning. The handler
// is registered for a thread that returns 0x77 and is started twice. The handler counts its runs,
// calls _sceKernelExitCallback with a value, and then sets a flag. One case passes -1 and one
// passes 0. Per case the test prints the result of each wait for the end, the exit status, the
// runs of the handler, whether the code after the call ran, and the result of unregistering the
// handler at the end.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define EXIT_STATUS 0x77
#define NOT_SET 0xDEAD

// Ends the calling callback with value as its return, like the return of a callback does
int _sceKernelExitCallback(int value);

static volatile int end_runs, exit_value, after_call;

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return EXIT_STATUS;
}

static __attribute__((noinline)) void exit_with_value(void) {
    _sceKernelExitCallback(exit_value);
}

// Counts its END runs and ends itself with exit_value
static int end_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    end_runs++;
    exit_with_value();
    after_call = 1;
    return 0;
}

static void run_case(const char *name, int value) {
    end_runs = 0;
    after_call = 0;
    exit_value = value;
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("end", target, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_handler, NULL);
    PRINTF(id < 0 ? id : 1, "%s: register", name);
    for (int start = 1; start <= 2; start++) {
        int status = NOT_SET;
        sceKernelStartThread(target, 0, NULL);
        SceUInt timeout = TEST_WAIT_US;
        PRINTF(sceKernelWaitThreadEnd(target, &status, &timeout), "%s: start %d: the wait for the end", name, start);
        PRINTF(status, "%s: start %d: the exit status", name, start);
        PRINTF(end_runs, "%s: start %d: runs of the handler", name, start);
        PRINTF(after_call, "%s: start %d: the code after the call ran", name, start);
    }
    if (id >= 0)
        PRINTF(sceKernelUnregisterThreadEventHandler(id), "%s: unregister it after", name);
    sceKernelDeleteThread(target);
}

int main(void) {
    run_case("exit with -1", -1);
    run_case("exit with 0", 0);

    test_exit();
    return 0;
}
