// Thread state as sceKernelGetThreadInfo reports it, and thread calls in less common cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID sema, target;
static int end_result[2], end_status[2];
static int arg_size, arg_null;

static int blocked_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 7;
}

static int end_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    end_result[i] = sceKernelWaitThreadEnd(target, &end_status[i], &timeout);
    return 0;
}

static int args_thread(SceSize args, void *argp) {
    arg_size = args;
    arg_null = argp == NULL;
    return 0;
}

// Prints the status and exit status sceKernelGetThreadInfo reports for thread
static void print_info(const char *name, SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    char label[64];
    sceClibSnprintf(label, sizeof(label), "info of a %s thread", name);
    PRINT(label, sceKernelGetThreadInfo(thread, &info));
    sceClibSnprintf(label, sizeof(label), "status of a %s thread", name);
    PRINT(label, info.status);
    sceClibSnprintf(label, sizeof(label), "exit status of a %s thread", name);
    PRINT(label, info.exitStatus);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 2, NULL);

    print_info("running", sceKernelGetThreadId());
    target = sceKernelCreateThread("target", blocked_thread, 0x10000100, 0x4000, 0, 0, NULL);
    print_info("new", target);
    sceKernelStartThread(target, 0, NULL);
    test_wait_until_waiting(target);
    print_info("waiting", target);

    // Two threads waiting for the same thread to end
    const SceUID first = test_start_thread_arg(end_waiter, 0);
    test_wait_until_waiting(first);
    const SceUID second = test_start_thread_arg(end_waiter, 1);
    test_wait_until_waiting(second);
    sceKernelSignalSema(sema, 1);
    test_join_thread(first);
    test_join_thread(second);
    PRINT("the first wait for the end", end_result[0]);
    PRINT("its status", end_status[0]);
    PRINT("the second wait for the end", end_result[1]);
    PRINT("its status", end_status[1]);
    print_info("ended", target);
    sceKernelDeleteThread(target);

    // Waiting for a thread that never started
    target = sceKernelCreateThread("dormant", blocked_thread, 0x10000100, 0x4000, 0, 0, NULL);
    SceUInt timeout = 1000;
    int status = 0xDEAD;
    PRINT("wait for the end of a thread that never started", sceKernelWaitThreadEnd(target, &status, &timeout));
    PRINT("its status", status);
    sceKernelDeleteThread(target);

    // Starting with no argument block
    const SceUID thread = sceKernelCreateThread("args", args_thread, 0x10000100, 0x4000, 0, 0, NULL);
    PRINT("start with no argument block", sceKernelStartThread(thread, 0, NULL));
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    PRINT("the argument size", arg_size);
    PRINT("the argument pointer is NULL", arg_null);
    PRINT("start with a size but no block", sceKernelStartThread(thread, 4, NULL));
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);

    // Stack and CPU queries
    const int free_size = sceKernelGetThreadStackFreeSize(0);
    PRINT("the stack free size is positive", free_size > 0);
    PRINT("the CPU affinity of the running thread", sceKernelGetThreadCpuAffinityMask(0));
    PRINT("change the affinity to core 0", sceKernelChangeThreadCpuAffinityMask(0, CPU_MASK_USER_0));
    PRINT("the affinity after the change", sceKernelGetThreadCpuAffinityMask(0));
    PRINT("change the affinity to 0", sceKernelChangeThreadCpuAffinityMask(0, 0));
    PRINT("change the affinity to a core games can't use", sceKernelChangeThreadCpuAffinityMask(0, 0x80000));

    test_exit();
    return 0;
}
