// Where a callback runs: on the stack of its thread, below the frame of the wait or check that ran
// it, how much stack each nesting level takes, and the thread and priority it runs with. Every
// address is printed as a comparison.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define THREAD_PRIORITY 0x70

static SceUID sema, outer_cb, nested_cb, owner;
static volatile SceUIntPtr stack_low, stack_high, wait_frame, check_frame, outer_frame, nested_frame, check_cb_frame;
static volatile int own_thread, own_priority, by_check;

static int nested_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    volatile int local = 0;
    nested_frame = (SceUIntPtr)&local;
    return 0;
}

// Runs the nested callback from inside itself, with a check
static int outer_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    volatile int local = 0;
    if (by_check) {
        check_cb_frame = (SceUIntPtr)&local;
        return 0;
    }
    outer_frame = (SceUIntPtr)&local;
    own_thread = sceKernelGetThreadId() == owner;
    own_priority = sceKernelGetThreadCurrentPriority();
    sceKernelNotifyCallback(nested_cb, 1);
    sceKernelCheckCallback();
    return 0;
}

static void check_from_here(void) {
    volatile int local = 0;
    check_frame = (SceUIntPtr)&local;
    by_check = 1;
    sceKernelNotifyCallback(outer_cb, 1);
    sceKernelCheckCallback();
}

static int callback_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    owner = sceKernelGetThreadId();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(owner, &info);
    stack_low = (SceUIntPtr)info.stack;
    stack_high = stack_low + info.stackSize;
    outer_cb = sceKernelCreateCallback("outer", 0, outer_callback, NULL);
    nested_cb = sceKernelCreateCallback("nested", 0, nested_callback, NULL);

    volatile int local = 0;
    wait_frame = (SceUIntPtr)&local;
    sceKernelNotifyCallback(outer_cb, 1);
    SceUInt timeout = 1000;
    sceKernelWaitSemaCB(sema, 1, &timeout);

    check_from_here();
    sceKernelDeleteCallback(outer_cb);
    sceKernelDeleteCallback(nested_cb);
    return 0;
}

static int on_stack(SceUIntPtr address) {
    return address >= stack_low && address < stack_high;
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID thread = sceKernelCreateThread("callbacks", callback_thread, THREAD_PRIORITY, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_join_thread(thread);

    PRINT("a callback runs on its thread's stack", on_stack(outer_frame));
    PRINT("below the frame of the wait that ran it", outer_frame < wait_frame);
    PRINT("less than 1 KiB below it", wait_frame - outer_frame < 0x400);
    PRINT("less than 4 KiB below it", wait_frame - outer_frame < 0x1000);
    PRINT("a nested callback runs on the same stack", on_stack(nested_frame));
    PRINT("below the callback that ran it", nested_frame < outer_frame);
    PRINT("less than 1 KiB below it", outer_frame - nested_frame < 0x400);
    PRINT("less than 4 KiB below it", outer_frame - nested_frame < 0x1000);
    PRINT("a callback run by a check runs on the same stack", on_stack(check_cb_frame));
    PRINT("below the frame of the check", check_cb_frame < check_frame);
    PRINT("less than 1 KiB below it", check_frame - check_cb_frame < 0x400);
    PRINT("it runs as its own thread", own_thread);
    PRINT("with the thread's priority", own_priority == THREAD_PRIORITY);

    test_exit();
    return 0;
}
