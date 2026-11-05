// sceKernelSetThreadContextForVM on a thread that is inside a syscall, where it writes the saved
// registers of the call. A thread waits on a semaphore from asm, with markers in r4 to r6 and a
// pointer to its results in r7. The test suspends it with sceKernelSuspendThreadForVM, reads its
// registers, and writes back the same ones with pc set to an ARM function, r0 = 0x1234 and
// r4 = 0x5678. The landing function stores r0, r4 and r6, and ends the thread. The test prints
// whether the next read shows the new registers, whether the landing ran, the registers it saw,
// and whether the code after the wait ran. It does this twice, once with the thread still
// waiting when it is resumed and signalled, and once with the semaphore signalled while the thread
// is suspended, so it is held at the end of the call when its registers are written.
//
// On a PS Vita TV on firmware 3.65 the write succeeds in both runs and the next read shows the new
// pc, r0 and r4. After the resume the landing function does not run and neither does the code
// after the wait, and the wait for the thread to end times out with 0x80028005. In the first run
// the thread is still waiting on the semaphore after the resume.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

int sceKernelSetThreadContextForVM(SceUID thid, const ThreadCpuRegisters *cpu, const ThreadVfpRegisters *vfp);

#define CPSR_THUMB 0x20
#define MARK_R0 0x1234
#define MARK_R4 0x5678

// What the thread stores. The asm below uses these offsets.
typedef struct Results {
    int wait_result;  // 0
    int tail_ran;     // 4
    int r0;           // 8
    int r4;           // 12
    int r6;           // 16
    int landed;       // 20
} Results;

static SceUID sema, target;
static volatile Results results;

// Waits on sema with a timeout, with r4 to r6 set to markers and r7 pointing to the results. After
// the wait it stores its result and a flag.
__attribute__((naked, target("arm"))) static int wait_marked(SceUID id, int count, SceUInt *timeout, volatile Results *out) {
    (void)id;
    (void)count;
    (void)timeout;
    (void)out;
    __asm__ volatile(
        "push {r3-r11, lr}\n\t"
        "mov r7, r3\n\t"
        "movw r4, #0x4444\n\t"
        "movt r4, #0x4444\n\t"
        "movw r5, #0x5555\n\t"
        "movt r5, #0x5555\n\t"
        "movw r6, #0x6666\n\t"
        "movt r6, #0x6666\n\t"
        "bl sceKernelWaitSema\n\t"
        "str r0, [r7, #0]\n\t"
        "mov r1, #1\n\t"
        "str r1, [r7, #4]\n\t"
        "pop {r3-r11, pc}\n\t");
}

// Where the test sends the thread. r7 points to the results, set by the test.
__attribute__((naked, target("arm"))) static void landing(void) {
    __asm__ volatile(
        "str r0, [r7, #8]\n\t"
        "str r4, [r7, #12]\n\t"
        "str r6, [r7, #16]\n\t"
        "mov r0, #1\n\t"
        "str r0, [r7, #20]\n\t"
        "mov r0, #0\n\t"
        "bl sceKernelExitThread\n\t");
}

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    return wait_marked(sema, 1, &timeout, &results);
}

// Whether the thread is held at the end of its call, by the status the suspend gives it
static int held(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(target, &info) == 0 && (info.status & 0x100);
}

static void run_case(const char *name, int signal_while_suspended) {
    sceClibMemset((void *)&results, 0, sizeof(results));
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    target = sceKernelCreateThread("target", target_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));

    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    PRINTF(sceKernelSuspendThreadForVM(target), "%s: suspend it", name);
    if (signal_while_suspended) {
        PRINTF(sceKernelSignalSema(sema, 1), "%s: signal the semaphore", name);
        TEST_WAIT_FOR(held() || results.tail_ran);
    }
    PRINTF(sceKernelGetThreadContextForVM(target, &cpu, NULL), "%s: read the context", name);
    cpu.reg[0] = MARK_R0;
    cpu.reg[4] = MARK_R4;
    cpu.reg[7] = (SceUInt32)&results;
    cpu.reg[15] = (SceUInt32)landing;
    cpu.cpsr &= ~CPSR_THUMB;
    PRINTF(sceKernelSetThreadContextForVM(target, &cpu, NULL), "%s: write the new registers", name);

    ThreadCpuRegisters back = { .size = sizeof(back) };
    PRINTF(sceKernelGetThreadContextForVM(target, &back, NULL), "%s: read it again", name);
    PRINTF(back.reg[15] == cpu.reg[15], "%s: the read shows the new pc", name);
    PRINTF(back.reg[0] == MARK_R0, "%s: the read shows the new r0", name);
    PRINTF(back.reg[4] == MARK_R4, "%s: the read shows the new r4", name);

    PRINTF(sceKernelResumeThreadForVM(target), "%s: resume it", name);
    if (!signal_while_suspended) {
        PRINTF(test_in_wait(target, WAIT_TYPE_SEMA, sema), "%s: it still waits on the semaphore", name);
        PRINTF(sceKernelSignalSema(sema, 1), "%s: signal the semaphore", name);
    }
    TEST_WAIT_FOR(results.landed || results.tail_ran);
    PRINTF(results.landed, "%s: the landing function ran", name);
    PRINTF(results.r0, "%s: r0 in the landing function", name);
    PRINTF(results.r4, "%s: r4 in the landing function", name);
    PRINTF(results.r6 == 0x66666666, "%s: r6 in the landing function is its marker", name);
    PRINTF(results.tail_ran, "%s: the code after the wait ran", name);
    SceUInt timeout = TEST_WAIT_US;
    PRINTF(sceKernelWaitThreadEnd(target, NULL, &timeout), "%s: wait for the thread to end", name);
    sceKernelDeleteThread(target);
    sceKernelDeleteSema(sema);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    run_case("resume, then signal", 0);
    run_case("signal, then resume", 1);
    test_exit();
    return 0;
}
