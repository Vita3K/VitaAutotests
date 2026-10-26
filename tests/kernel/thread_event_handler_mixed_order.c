// Four thread event handlers for START and END, each logging its number when it runs for the
// target. Handlers 1 and 3 are registered for the target and handlers 2 and 4 for every thread
// (0x10027).
// Run 1 registers them in the order 1, 2, 3, 4, then starts the target. Run 2 is a new target, with
// them registered in the order 4, 3, 2, 1. Per run the test prints whether the START runs and the
// END runs came in the registration order, the order of the runs as hex digits, and each handler's
// START and END counts for the target.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define HANDLERS 4

static SceUID target;
static volatile int log_count, log_handler[2 * HANDLERS], log_type[2 * HANDLERS];

// Logs the number in common for a run of the target
static int handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)arg;
    if (thread_id == target && log_count < 2 * HANDLERS) {
        log_handler[log_count] = (int)(SceUIntPtr)common;
        log_type[log_count] = type;
        log_count++;
    }
    return 0;
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Handler number 1 and 3 are for the target, the others for every thread
static SceUID register_number(int number) {
    const SceUID thread = number % 2 ? target : SCE_KERNEL_THREAD_ID_PROCESS_ALL_ID;
    return sceKernelRegisterThreadEventHandler("handler", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, handler, (void *)(SceUIntPtr)number);
}

// Gathers the handlers that ran with type, in run order, as hex digits, and whether they came in
// the registration order
static unsigned order_of(SceInt32 type, const int numbers[HANDLERS], int *in_order) {
    unsigned digits = 0;
    int count = 0;
    *in_order = 1;
    for (int i = 0; i < log_count; i++) {
        if (log_type[i] != type)
            continue;
        if (count >= HANDLERS || log_handler[i] != numbers[count])
            *in_order = 0;
        digits = digits * 16 + log_handler[i];
        count++;
    }
    *in_order = *in_order && count == HANDLERS;
    return digits;
}

static void run(const char *name, const int numbers[HANDLERS]) {
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    SceUID ids[HANDLERS];
    for (int i = 0; i < HANDLERS; i++) {
        ids[i] = register_number(numbers[i]);
        PRINTF(ids[i] < 0 ? ids[i] : 1, "%s: register handler %d", name, numbers[i]);
    }

    log_count = 0;
    sceKernelStartThread(target, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: the wait for the end", name);

    int in_order;
    const unsigned start_digits = order_of(SCE_KERNEL_THREAD_EVENT_TYPE_START, numbers, &in_order);
    PRINTF(in_order, "%s: START order equals the registration order", name);
    PRINTF(start_digits, "%s: START order", name);
    const unsigned end_digits = order_of(SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, numbers, &in_order);
    PRINTF(in_order, "%s: END order equals the registration order", name);
    PRINTF(end_digits, "%s: END order", name);
    for (int k = 1; k <= HANDLERS; k++) {
        int starts = 0, ends = 0;
        for (int i = 0; i < log_count; i++) {
            if (log_handler[i] != k)
                continue;
            if (log_type[i] == SCE_KERNEL_THREAD_EVENT_TYPE_START)
                starts++;
            else
                ends++;
        }
        PRINTF(starts, "%s: handler %d: START runs for the target", name, k);
        PRINTF(ends, "%s: handler %d: END runs for the target", name, k);
    }

    for (int i = 0; i < HANDLERS; i++) {
        if (ids[i] >= 0)
            PRINTF(sceKernelUnregisterThreadEventHandler(ids[i]), "%s: unregister handler %d", name, numbers[i]);
    }
    sceKernelDeleteThread(target);
}

int main(void) {
    static const int first[HANDLERS] = { 1, 2, 3, 4 };
    static const int second[HANDLERS] = { 4, 3, 2, 1 };
    run("run 1, registered 1 2 3 4", first);
    run("run 2, registered 4 3 2 1", second);

    test_exit();
    return 0;
}
