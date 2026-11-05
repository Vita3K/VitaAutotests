// What sceKernelGetThreadContextForVM reports for a thread that was started but never ran. W is a
// VM thread on core 0 that waits behind a higher priority thread G, which spins on core 0, so W is
// ready and has run no instruction. The main thread, on core 1, suspends W with
// sceKernelSuspendThreadForVM, reads its status and its registers, then ends G and waits 50 ms to
// see whether W runs although its core is free. Then it resumes W. Printed are the results and the
// status, comparisons for the pc, the stack pointer and r1, the mode and Thumb bit of CPSR, and a
// mask of the registers r2 to r12 that hold 0xDEADBEEF. W records the arguments it is called with
// so the registers can be compared with them.
//
// On a PS Vita TV on firmware 3.65 W has not run when it is suspended, the suspend returns 1 and
// the status is SUSPENDED 0x100, and the read succeeds. The pc is the entry function and the Thumb
// bit of CPSR is bit 0 of its address. The mode is 0x10, r0 is 4, the size of the arguments, and r1
// is the argument pointer. Both r1 and sp are 0x10 below the top of the stack. Registers r2 to r12
// all hold 0xDEADBEEF. The status stays 0x100 after the read, and W does not run while it is
// suspended, even after G ended and its core is free. After the resume W runs with the argument
// size 4 and the value that was passed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define HIGH_PRIORITY (SCE_KERNEL_DEFAULT_PRIORITY_USER - 2)
#define ARG_VALUE 0x1234

static volatile int release_gate, g_up, w_ran;
static volatile SceSize w_args;
static void *volatile w_argp;
static volatile int w_value;

// G spins on core 0 until the test lets it go, or for at most 5 seconds
static int g_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    g_up = 1;
    const SceInt64 end = sceKernelGetSystemTimeWide() + 5000000;
    while (!release_gate && sceKernelGetSystemTimeWide() < end)
        ;
    return 0;
}

static int w_entry(SceSize args, void *argp) {
    w_args = args;
    w_argp = argp;
    w_value = args >= sizeof(int) ? *(int *)argp : 0;
    w_ran = 1;
    return 0;
}

// Waits for thread to end, for at most TEST_WAIT_US, then deletes it
static void join_bounded(SceUID thread) {
    SceUInt32 timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);
}

static int status_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? (int)info.status : -1;
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    const SceUID g = sceKernelCreateThread("g", g_entry, HIGH_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(g, 0, NULL);
    TEST_WAIT_FOR(g_up);

    const SceUID w = sceKernelCreateThread("w", w_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(w, &info);
    const SceUIntPtr stack_top = (SceUIntPtr)info.stack + info.stackSize;
    int arg = ARG_VALUE;
    sceKernelStartThread(w, sizeof(arg), &arg);

    const int ran_before = w_ran;
    const int suspend = sceKernelSuspendThreadForVM(w);
    const int status = status_of(w);
    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    const int read = sceKernelGetThreadContextForVM(w, &cpu, NULL);
    const int status_after_read = status_of(w);

    // Lets W run if it can, and gives it plenty of time to do so
    release_gate = 1;
    join_bounded(g);
    sceKernelDelayThread(50000);
    const int ran_after_gate = w_ran;
    const int status_after_gate = status_of(w);

    const int resume = sceKernelResumeThreadForVM(w);
    TEST_WAIT_FOR(w_ran);
    const int ran_after_resume = w_ran;
    join_bounded(w);

    PRINT("W ran before it was suspended", ran_before);
    PRINT("suspend W", suspend);
    PRINT("its status", status);
    PRINT("read its registers", read);
    if (read == 0) {
        unsigned int deadbeef = 0;
        for (int i = 2; i <= 12; i++)
            deadbeef |= (cpu.reg[i] == 0xDEADBEEF) << i;
        PRINT("pc is the entry function, ignoring the Thumb bit", (cpu.reg[15] & ~1u) == ((SceUIntPtr)w_entry & ~1u));
        PRINT("CPSR Thumb bit is the entry's address bit 0", ((cpu.cpsr >> 5) & 1) == ((SceUIntPtr)w_entry & 1));
        PRINT("CPSR mode", cpu.cpsr & 0x1F);
        PRINT("r0", cpu.reg[0]);
        PRINT("r1 is the argument pointer W gets", cpu.reg[1] == (SceUIntPtr)w_argp);
        PRINT("r1, below the stack top by", stack_top - cpu.reg[1]);
        PRINT("sp, below the stack top by", stack_top - cpu.reg[13]);
        PRINT("mask of r2 to r12 that hold 0xDEADBEEF", deadbeef);
    }
    PRINT("its status after the read", status_after_read);
    PRINT("W ran after G ended and its core was free", ran_after_gate);
    PRINT("its status after that", status_after_gate);
    PRINT("resume W", resume);
    PRINT("W ran after the resume", ran_after_resume);
    PRINT("W's argument size", w_args);
    PRINT("W's argument value is the one passed", w_value == ARG_VALUE);
    test_exit();
    return 0;
}
