// The VFP exception setting in nested callbacks: what a callback starts with when the thread and
// the callback that ran it each set one of their own, what a change returns and leaves inside each
// level, and what each level reads back once the level inside it returned. The callbacks run from
// a check and from a wait with callbacks on a semaphore that has a count, with fresh objects in
// each case. Every value is recorded, the thread clears its setting, and only then the values are
// printed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID nested_cb;
static volatile unsigned int outer_entry, outer_change, outer_after_change, outer_after_nested;
static volatile unsigned int nested_entry, nested_change, nested_after_change;
static volatile int nested_check;

static int nested_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    nested_entry = sceKernelGetCurrentThreadVfpException();
    // Returns the setting before the change
    nested_change = sceKernelChangeThreadVfpException(0, SCE_KERNEL_VFP_EXCP_OFC);
    nested_after_change = sceKernelGetCurrentThreadVfpException();
    return 0;
}

static int outer_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    outer_entry = sceKernelGetCurrentThreadVfpException();
    outer_change = sceKernelChangeThreadVfpException(0, SCE_KERNEL_VFP_EXCP_DZC);
    outer_after_change = sceKernelGetCurrentThreadVfpException();
    sceKernelNotifyCallback(nested_cb, 1);
    nested_check = sceKernelCheckCallback();
    outer_after_nested = sceKernelGetCurrentThreadVfpException();
    return 0;
}

static void run(const char *name, int use_wait) {
    outer_entry = outer_change = outer_after_change = outer_after_nested = 0xDEAD;
    nested_entry = nested_change = nested_after_change = 0xDEAD;
    nested_check = 0xDEAD;
    const SceUID outer_cb = sceKernelCreateCallback("outer", 0, outer_callback, NULL);
    nested_cb = sceKernelCreateCallback("nested", 0, nested_callback, NULL);
    // One count, so the wait takes it at once after running the callbacks
    const SceUID sema = sceKernelCreateSema("count", 0, 1, 1, NULL);

    // The thread's own setting, which no callback should see
    sceKernelChangeThreadVfpException(SCE_KERNEL_VFP_EXCP_ALL, 0);
    sceKernelChangeThreadVfpException(0, SCE_KERNEL_VFP_EXCP_QC);
    const unsigned int before = sceKernelGetCurrentThreadVfpException();
    sceKernelNotifyCallback(outer_cb, 1);
    SceUInt timeout = TEST_WAIT_US;
    const int result = use_wait ? sceKernelWaitSemaCB(sema, 1, &timeout) : sceKernelCheckCallback();
    const unsigned int after = sceKernelGetCurrentThreadVfpException();
    sceKernelChangeThreadVfpException(SCE_KERNEL_VFP_EXCP_ALL, 0);

    sceKernelDeleteSema(sema);
    sceKernelDeleteCallback(nested_cb);
    sceKernelDeleteCallback(outer_cb);

    PRINTF(test_created(outer_cb) | test_created(nested_cb) | test_created(sema), "%s: create the objects", name);
    PRINTF(before, "%s: the thread's setting before", name);
    PRINTF(result, "%s: the call", name);
    PRINTF(outer_entry, "%s: the callback: the setting it starts with", name);
    PRINTF(outer_change, "%s: the callback: its change returns", name);
    PRINTF(outer_after_change, "%s: the callback: the setting after its change", name);
    PRINTF(nested_check, "%s: the callback: its check", name);
    PRINTF(nested_entry, "%s: the nested callback: the setting it starts with", name);
    PRINTF(nested_change, "%s: the nested callback: its change returns", name);
    PRINTF(nested_after_change, "%s: the nested callback: the setting after its change", name);
    PRINTF(outer_after_nested, "%s: the callback: the setting after the nested one returned", name);
    PRINTF(after, "%s: the thread's setting after", name);
}

int main(void) {
    run("a check", 0);
    run("a wait with callbacks", 1);

    test_exit();
    return 0;
}
