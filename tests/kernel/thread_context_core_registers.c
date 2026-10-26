// The core registers sceKernelGetThreadContextForVM reports for a thread that is suspended in the
// middle of its own work. A thread waits in a semaphore wait, called from asm with markers in r4 to
// r11 and flags from a compare, and another spins in a Thumb loop, then a third in an ARM loop. The
// spinning threads hold markers in r0 to r11 and lr, flags from a compare, and Q and GE set. Each
// one is suspended while it runs, read from another core, resumed, and then ended, and the
// registers it holds after that are printed too. Printed are masks of the registers that hold
// their markers, comparisons for sp, lr and pc, and the flags and Thumb bit of CPSR. The argument
// registers r0 to r3 of the wait are not checked, since the syscall may change them.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

// The value each register holds in the asm, indexed like ThreadCpuRegisters.reg: r0 to r11, then
// sp, lr and pc, of which only lr has a marker
static const unsigned int markers[16] = {
    0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888,
    0x99999999, 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0, 0, 0xEEEEEEEE, 0,
};

// The flags in CPSR this test prints: N, Z, C, V, Q and GE
#define CPSR_FLAGS_QGE 0xF80F0000

// Sets a register to its marker with movw and movt, which change no flags
#define MARK(reg, half) "movw " reg ", #0x" half "\n\t" "movt " reg ", #0x" half "\n\t"

// What the waiting thread stores: sp before its wait, its registers r4 to r11 after it, and the
// wait's result
typedef struct Waiting {
    unsigned int sp;
    unsigned int reg[16];
    int result;
} Waiting;

// What a spinning thread stores: sp in its loop, a word the loop writes while it runs, the stop
// flag main sets, and its registers r0 to r11 and lr after the loop
typedef struct Spinning {
    unsigned int sp;
    unsigned int spinning;
    unsigned int stop;
    unsigned int after[16];
} Spinning;

static SceUID sema;
static Waiting waiting_out;
static Spinning spin_out;

// The ends of the loops of the spinning threads, and of the wait's asm
extern const char wait_marked_begin[], wait_marked_end[];
extern const char thumb_loop_begin[], thumb_loop_end[];
extern const char arm_loop_begin[], arm_loop_end[];

// Waits on sema with a timeout, called from asm with markers in r4 to r11 and flags from a cmp.
// It stores sp in the Waiting out before the wait, and r4 to r11 and the result after it
__attribute__((naked, target("thumb"))) static int wait_marked(SceUID id, int count, SceUInt *timeout, Waiting *out) {
    (void)id;
    (void)count;
    (void)timeout;
    (void)out;
    __asm__ volatile(
        ".globl wait_marked_begin\n\t"
        "wait_marked_begin:\n\t"
        "push {r3-r11, lr}\n\t"
        "str sp, [r3]\n\t"
        MARK("r4", "5555")
        MARK("r5", "6666")
        MARK("r6", "7777")
        MARK("r7", "8888")
        MARK("r8", "9999")
        MARK("r9", "AAAA")
        MARK("r10", "BBBB")
        MARK("r11", "CCCC")
        "cmp r4, r5\n\t"
        "bl sceKernelWaitSema\n\t"
        "ldr r3, [sp]\n\t"
        "add r3, r3, #20\n\t"
        "stmia r3, {r4-r11}\n\t"
        "str r0, [r3, #48]\n\t"
        "pop {r3-r11, pc}\n\t"
        ".globl wait_marked_end\n\t"
        "wait_marked_end:\n\t");
}

// Waits on sema for the waiting thread, with its timeout
static int waiting_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    return wait_marked(sema, 1, &timeout, &waiting_out);
}

// Spins in Thumb code until main sets stop to 1. The argument is a pointer to a Spinning, which the
// entry keeps on the stack. The loop reads it from there and branches on stop with a table branch,
// which changes no flags, so the flags are still the ones the entry set. Only r12 is a scratch
// register in the loop.
__attribute__((naked, target("thumb"))) static int thumb_spin(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "ldr r1, [r1]\n\t"
        "push {r1, r4-r11, lr}\n\t"
        "str sp, [r1]\n\t"
        // Q, then GE, which set no N, Z, C or V
        "movw r12, #0xFFFF\n\t"
        "movt r12, #0xFFFF\n\t"
        "uadd8 r12, r12, r12\n\t"
        "movw r12, #0xFFFF\n\t"
        "movt r12, #0x7FFF\n\t"
        "ssat r12, #1, r12\n\t"
        MARK("r0", "1111")
        MARK("r1", "2222")
        MARK("r2", "3333")
        MARK("r3", "4444")
        MARK("r4", "5555")
        MARK("r5", "6666")
        MARK("r6", "7777")
        MARK("r7", "8888")
        MARK("r8", "9999")
        MARK("r9", "AAAA")
        MARK("r10", "BBBB")
        MARK("r11", "CCCC")
        MARK("lr", "EEEE")
        "cmp r4, r5\n\t"
        ".globl thumb_loop_begin\n\t"
        "thumb_loop_begin:\n\t"
        "1:\n\t"
        "ldr r12, [sp]\n\t"
        "str lr, [r12, #4]\n\t"
        "ldr r12, [r12, #8]\n\t"
        // Table of two halfwords: stop 0 goes back to 1 and stop 1 falls through to the exit
        "tbb [pc, r12]\n\t"
        ".byte 1, 2\n\t"
        "b.n 1b\n\t"
        ".globl thumb_loop_end\n\t"
        "thumb_loop_end:\n\t"
        "ldr r12, [sp]\n\t"
        "add r12, r12, #12\n\t"
        "stmia r12, {r0-r11}\n\t"
        "str lr, [r12, #56]\n\t"
        "movs r0, #0\n\t"
        "pop {r1, r4-r11, pc}\n\t");
}

// The same as thumb_spin, in ARM code, with a branch through pc instead of the table
__attribute__((naked, target("arm"))) static int arm_spin(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "ldr r1, [r1]\n\t"
        "push {r1, r4-r11, lr}\n\t"
        "str sp, [r1]\n\t"
        "movw r12, #0xFFFF\n\t"
        "movt r12, #0xFFFF\n\t"
        "uadd8 r12, r12, r12\n\t"
        "movw r12, #0xFFFF\n\t"
        "movt r12, #0x7FFF\n\t"
        "ssat r12, #1, r12\n\t"
        MARK("r0", "1111")
        MARK("r1", "2222")
        MARK("r2", "3333")
        MARK("r3", "4444")
        MARK("r4", "5555")
        MARK("r5", "6666")
        MARK("r6", "7777")
        MARK("r7", "8888")
        MARK("r8", "9999")
        MARK("r9", "AAAA")
        MARK("r10", "BBBB")
        MARK("r11", "CCCC")
        MARK("lr", "EEEE")
        "cmp r4, r5\n\t"
        ".globl arm_loop_begin\n\t"
        "arm_loop_begin:\n\t"
        "1:\n\t"
        "ldr r12, [sp]\n\t"
        "str lr, [r12, #4]\n\t"
        "ldr r12, [r12, #8]\n\t"
        // Stop 0 goes to the branch back, stop 1 to the branch out, eight bytes further on
        "add pc, pc, r12, lsl #2\n\t"
        "nop\n\t"
        "b 1b\n\t"
        "b 2f\n\t"
        ".globl arm_loop_end\n\t"
        "arm_loop_end:\n\t"
        "2:\n\t"
        "ldr r12, [sp]\n\t"
        "add r12, r12, #12\n\t"
        "stmia r12, {r0-r11}\n\t"
        "str lr, [r12, #56]\n\t"
        "mov r0, #0\n\t"
        "pop {r1, r4-r11, pc}\n\t");
}

// Returns one bit per register from first to last that holds its marker
static unsigned int kept(const unsigned int *reg, int first, int last) {
    unsigned int mask = 0;
    for (int i = first; i <= last; i++) {
        if (reg[i] == markers[i])
            mask |= 1U << i;
    }
    return mask;
}

// Returns whether pc, with its Thumb bit cleared, is from begin up to end
static int in_range(unsigned int pc, const char *begin, const char *end) {
    pc &= ~1U;
    return pc >= (unsigned int)(SceUIntPtr)begin && pc < (unsigned int)(SceUIntPtr)end;
}

static void waiting_case(void) {
    const SceUID target = sceKernelCreateThread("waiting", waiting_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(target, WAIT_TYPE_SEMA, sema));

    PRINT("waiting: suspend it", sceKernelSuspendThreadForVM(target));
    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    ThreadVfpRegisters vfp = { .size = sizeof(vfp) };
    PRINT("waiting: read it", sceKernelGetThreadContextForVM(target, &cpu, &vfp));
    PRINT("waiting: r4 to r11 that hold their markers, one bit each", kept(cpu.reg, 4, 11));
    PRINT("waiting: sp is the one before the wait", cpu.reg[13] == waiting_out.sp);
    PRINT("waiting: pc & 1", cpu.reg[15] & 1);
    PRINT("waiting: pc inside the asm", in_range(cpu.reg[15], wait_marked_begin, wait_marked_end));
    PRINT("waiting: the cpsr flags", cpu.cpsr & CPSR_FLAGS_QGE);
    PRINT("waiting: the Thumb bit", (cpu.cpsr >> 5) & 1);

    PRINT("waiting: resume it", sceKernelResumeThreadForVM(target));
    PRINT("waiting: signal it", sceKernelSignalSema(sema, 1));
    test_join_thread(target);
    PRINT("waiting: the wait's result", waiting_out.result);
    PRINT("waiting: r4 to r11 after the wait, one bit each", kept(waiting_out.reg, 4, 11));
}

static void spin_case(const char *name, SceKernelThreadEntry entry, const char *loop_begin, const char *loop_end) {
    sceClibMemset(&spin_out, 0, sizeof(spin_out));
    Spinning *arg = &spin_out;
    const SceUID target = sceKernelCreateThread(name, entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(target, sizeof(arg), &arg);
    TEST_WAIT_FOR(spin_out.spinning != 0);

    PRINTF(sceKernelSuspendThreadForVM(target), "%s: suspend it", name);
    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    ThreadVfpRegisters vfp = { .size = sizeof(vfp) };
    PRINTF(sceKernelGetThreadContextForVM(target, &cpu, &vfp), "%s: read it", name);
    PRINTF(kept(cpu.reg, 0, 11), "%s: r0 to r11 that hold their markers, one bit each", name);
    PRINTF(cpu.reg[14] == markers[14], "%s: lr holds its marker", name);
    PRINTF(in_range(cpu.reg[15], loop_begin, loop_end), "%s: pc inside the loop", name);
    PRINTF(cpu.reg[13] == spin_out.sp, "%s: sp is the one before the loop", name);
    PRINTF(cpu.cpsr & CPSR_FLAGS_QGE, "%s: the cpsr flags", name);
    PRINTF((cpu.cpsr >> 5) & 1, "%s: the Thumb bit", name);

    PRINTF(sceKernelResumeThreadForVM(target), "%s: resume it", name);
    spin_out.stop = 1;
    test_join_thread(target);
    PRINTF(kept(spin_out.after, 0, 11), "%s: r0 to r11 after the loop, one bit each", name);
    PRINTF(spin_out.after[14] == markers[14], "%s: lr after the loop holds its marker", name);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);

    // The waiting thread is read from its own core, as thread_context_vfp does
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelDelayThread(1000);
    waiting_case();

    // The spinning threads run on core 1 and are read from core 2
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelDelayThread(1000);
    spin_case("a Thumb loop", thumb_spin, thumb_loop_begin, thumb_loop_end);
    spin_case("an ARM loop", arm_spin, arm_loop_begin, arm_loop_end);

    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
