// Whether the result of a wait that ends while sceKernelSuspendThreadForVM holds the waiter is
// already in the registers sceKernelGetThreadContextForVM reads, or only shows once the thread is
// resumed. W has the VM attribute and calls sceKernelWaitSema on a semaphore with count 0, then
// stores the call's result. The main thread suspends W while it waits and reads its context, then
// ends the wait while W stays suspended, once with a signal and once with a timeout, and reads the
// context again. Each read after the first prints, compared with the first read:
// - whether the pc, the sp and the CPSR are the same
// - whether r0 is 0, the same as before, or the timeout error 0x80028005
// Each read also prints whether it shows a thread inside a call, which a waiting thread does with
// the FPSCR control bits 0x03000000 and d0 reading the NaN 0x7FF8DEAD7F80DEAD (see
// thread_context_vfp). The test prints what the semaphore and W report after the wait ended, and W's
// stored result before and after the resume.
//
// On a PS Vita TV on firmware 3.65 the context of a suspended W does not change when its wait
// ends. Every read after the first has the pc, sp, CPSR and r0 of the first read, with an r0 that
// is neither 0 nor the timeout error, and every read has the FPSCR control bits 0x03000000 and
// the NaN in d0. After the wait ended, the semaphore has no waiter and count 0, and W reports
// SUSPENDED 0x100 with wait type 0. W's stored result is still 0xDEAD while it is suspended, and
// after the resume it is 0 for the signal and 0x80028005 for the timeout.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

#define NOT_SET 0xDEAD
#define TIMEOUT_ERROR 0x80028005

static SceUID sema, target;
static SceUInt wait_timeout;
static volatile int result;
static ThreadCpuRegisters first;

static int wait_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    result = sceKernelWaitSema(sema, 1, wait_timeout ? &wait_timeout : NULL);
    return 0;
}

static int sema_waiters(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.numWaitThreads;
}

static int sema_count(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.currentCount;
}

static int target_status(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(target, &info);
    return info.status;
}

static int target_wait_type(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(target, &info);
    return info.waitType;
}

// One read of W's context
typedef struct Snapshot {
    int read;
    ThreadCpuRegisters cpu;
    ThreadVfpRegisters vfp;
} Snapshot;

// Reads W's context without printing, so W can't run on this core in between
static void take(Snapshot *snap) {
    snap->cpu = (ThreadCpuRegisters){ .size = sizeof(snap->cpu) };
    snap->vfp = (ThreadVfpRegisters){ .size = sizeof(snap->vfp) };
    snap->read = sceKernelGetThreadContextForVM(target, &snap->cpu, &snap->vfp);
}

// Prints a read of W's context. The first read is the one the others compare with.
static void report(const char *name, const char *when, const Snapshot *snap, int is_first) {
    const ThreadCpuRegisters cpu = snap->cpu;
    const ThreadVfpRegisters vfp = snap->vfp;
    PRINTF(snap->read, "%s, %s: the read", name, when);
    PRINTF((vfp.fpscr & FPSCR_CONTROL) == 0x03000000, "%s, %s: FPSCR control bits are the call's", name, when);
    PRINTF(vfp.reg[0] == 0x7F80DEAD && vfp.reg[1] == 0x7FF8DEAD, "%s, %s: d0 is the NaN of a call", name, when);
    if (is_first) {
        first = cpu;
        return;
    }
    PRINTF(cpu.reg[15] == first.reg[15], "%s, %s: pc is the first read's", name, when);
    PRINTF(cpu.reg[13] == first.reg[13], "%s, %s: sp is the first read's", name, when);
    PRINTF(cpu.cpsr == first.cpsr, "%s, %s: CPSR is the first read's", name, when);
    PRINTF(cpu.reg[0] == 0, "%s, %s: r0 is 0", name, when);
    PRINTF(cpu.reg[0] == first.reg[0], "%s, %s: r0 is the first read's", name, when);
    PRINTF(cpu.reg[0] == TIMEOUT_ERROR, "%s, %s: r0 is the timeout error", name, when);
}

static void run(const char *name, int signal) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    result = NOT_SET;
    wait_timeout = signal ? 0 : TEST_WAIT_US;
    target = sceKernelCreateThread("target", wait_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));
    PRINTF(sceKernelSuspendThreadForVM(target), "%s: suspend it", name);
    // VFP use on the target's core, so the kernel saved its VFP registers
    volatile double x = 1.5, y = 2.25;
    volatile double z = x * y;
    (void)z;
    Snapshot snap;
    take(&snap);
    report(name, "waiting", &snap, 1);

    if (signal) {
        const int signaled = sceKernelSignalSema(sema, 1);
        take(&snap);
        PRINTF(signaled, "%s: signal it while it is suspended", name);
        report(name, "right after the signal", &snap, 0);
    }
    // The wait is over once the semaphore has no waiter left, for the signal or for the timeout
    TEST_WAIT_FOR(sema_waiters() == 0);
    PRINTF(sema_waiters(), "%s: waiters of the semaphore", name);
    PRINTF(sema_count(), "%s: count of the semaphore", name);
    PRINTF(target_status() == SCE_THREAD_SUSPENDED, "%s: W reports SUSPENDED", name);
    PRINTF(target_wait_type(), "%s: W's wait type", name);
    take(&snap);
    report(name, "the wait is over", &snap, 0);
    PRINTF(result, "%s: W's result while it is suspended", name);

    PRINTF(sceKernelResumeThreadForVM(target), "%s: resume it", name);
    test_join_thread(target);
    PRINTF(result, "%s: W's result after the resume", name);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run("a signal", 1);
    run("a timeout", 0);
    test_exit();
    return 0;
}
