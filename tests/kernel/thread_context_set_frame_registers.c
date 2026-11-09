// Which registers a thread has at the end of its syscall after sceKernelSetThreadContextForVM wrote
// the saved registers of that call. A thread waits on a semaphore. The test suspends it with
// sceKernelSuspendThreadForVM, reads its registers, and writes them back with pc set to an ARM
// landing function, r0 to r3 set to 0xA0A0A0A0 to 0xA3A3A3A3 and r4 to r11 set to 0xB4B4B4B4 to
// 0xBBBBBBBB. sp and lr stay as they were. Then it resumes the thread and signals the semaphore.
// The landing function takes no address from a register: it pushes r0 to r12, copies them to a
// global through an address from its literal pool, sets a flag and ends the thread. The test prints
// whether the landing ran and, for each register, its value or whether it holds a set value.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int sceKernelSetThreadContextForVM(SceUID thid, const ThreadCpuRegisters *cpu, const ThreadVfpRegisters *vfp);

#define CPSR_THUMB 0x20
#define DEAD 0xDEADBEEF
#define NOT_RUN 0xDEAD

static SceUID sema;
// r0 to r12 as the landing function saw them, then the flag it sets
volatile unsigned int landing_regs[14];

// Where the test sends the thread
__attribute__((naked, target("arm"))) static void landing(void) {
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "ldr r4, 2f\n\t"
        "mov r5, #0\n\t"
        "1:\n\t"
        "ldr r6, [sp, r5, lsl #2]\n\t"
        "str r6, [r4, r5, lsl #2]\n\t"
        "add r5, r5, #1\n\t"
        "cmp r5, #13\n\t"
        "blt 1b\n\t"
        "mov r6, #1\n\t"
        "str r6, [r4, #52]\n\t"
        "bic sp, sp, #7\n\t"
        "mov r0, #0\n\t"
        "bl sceKernelExitThread\n\t"
        "2:\n\t"
        ".word landing_regs\n\t");
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    return sceKernelWaitSema(sema, 1, &timeout);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    for (int i = 0; i < 13; i++)
        landing_regs[i] = NOT_RUN;
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));

    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    const int suspend = sceKernelSuspendThreadForVM(target);
    const int read = sceKernelGetThreadContextForVM(target, &cpu, NULL);
    for (int i = 0; i < 4; i++)
        cpu.reg[i] = 0xA0A0A0A0 + 0x01010101 * i;
    for (int i = 4; i < 12; i++)
        cpu.reg[i] = 0xB4B4B4B4 + 0x01010101 * (i - 4);
    cpu.reg[15] = (SceUInt32)landing;
    cpu.cpsr &= ~CPSR_THUMB;
    const int write = sceKernelSetThreadContextForVM(target, &cpu, NULL);
    const int resume = sceKernelResumeThreadForVM(target);
    const int signal = sceKernelSignalSema(sema, 1);
    TEST_WAIT_FOR(landing_regs[13]);
    SceUInt timeout = TEST_WAIT_US;
    const int end = sceKernelWaitThreadEnd(target, NULL, &timeout);

    PRINT("suspend it", suspend);
    PRINT("read the context", read);
    PRINT("write the new registers", write);
    PRINT("resume it", resume);
    PRINT("signal the semaphore", signal);
    PRINT("the landing function ran", landing_regs[13]);
    PRINT("r0 is 0, the result of the wait", landing_regs[0] == 0);
    PRINT("r0 holds the set r0", landing_regs[0] == cpu.reg[0]);
    PRINT("r1 is 0xDEADBEEF", landing_regs[1] == DEAD);
    PRINT("r2 is 0xDEADBEEF", landing_regs[2] == DEAD);
    PRINT("r3 is 0xDEADBEEF", landing_regs[3] == DEAD);
    PRINT("r4 is 0, the result of the wait", landing_regs[4] == 0);
    for (int i = 4; i < 8; i++)
        PRINTF(landing_regs[i] == cpu.reg[i], "r%d holds the set r%d", i, i);
    for (int i = 8; i < 12; i++)
        PRINTF(landing_regs[i] == cpu.reg[i - 8], "r%d holds the set r%d", i, i - 8);
    for (int i = 8; i < 12; i++)
        PRINTF(landing_regs[i] == cpu.reg[i], "r%d holds the set r%d", i, i);
    PRINT("r12 is 0xDEADBEEF", landing_regs[12] == DEAD);
    PRINT("wait for the thread to end", end);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
