// What sceKernelGetThreadContextForVM reports for a thread that is stopped right at a system call,
// across many suspends. W has the VM attribute and loops on core 0: a short spin in its own ARM
// code, a counter increment, then sceKernelPollSema on an empty semaphore, which returns at once.
// The main thread, on core 1, does rounds of suspending W, reading its registers, resuming it and
// waiting until its counter moved. The context has no flag for a thread that is in a system call.
// What differs is its FPSCR control bits, which read 0x03000000 instead of the value W set. A
// sample with that difference is called a kernel view here. The test does at least 50 rounds, and
// more until it caught a kernel view sample, up to 2000. It sorts each sample's pc into the range
// of W's loop and the range of the import stub of sceKernelPollSema, and prints only facts that
// hold for every sample. How many samples are caught in the stub or in the loop is a matter of
// timing, and so is whether a sample at the stub has the kernel view, since W may be stopped
// before it enters the call.
//
// On a PS Vita TV on firmware 3.65 a kernel view sample is caught, every suspend returns 1, every
// read and resume returns 0, and the counter moves after every resume. Every sample is in user
// mode with sp on W's stack and the pc in the loop or in the stub, and d8 to d15 hold the values W
// set. Every sample with the kernel view has its pc in the stub, the FPSCR control bits
// 0x03000000, and the same pc as the other kernel view samples.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

#define MIN_ROUNDS 50
#define MAX_ROUNDS 2000
// The size of an import stub
#define STUB_SIZE 16
// The FPSCR control bits W sets, round towards minus infinity on top of the default 0x03000000
#define OWN_FPSCR 0x03800000
#define KERNEL_FPSCR 0x03000000

// What W reads through its argument: the counter it increments, the flag that stops it, the empty
// semaphore it polls, and the values of d0 to d31 it sets before its loop
typedef struct Shared {
    volatile unsigned int counter;
    volatile unsigned int stop;
    SceUID sema;
    unsigned int pad;
    unsigned long long values[32];
} Shared;

typedef struct Sample {
    int suspend, read, resume, moved;
    unsigned int mode, pc, fpscr;
    int sp_on_stack, kernel_view, own_d8_d15;
} Sample;

static Shared shared;
static Sample samples[MAX_ROUNDS];

// The first and the end of W's own code in the loop
extern const char vm_boundary_begin[], vm_boundary_end[];

// Sets the VFP registers and FPSCR, then loops in ARM code with a call to sceKernelPollSema in it
// until shared.stop is set. The argument is a pointer to the Shared. It puts back d8 to d15 and
// FPSCR before it returns. The pushes keep sp 8-byte aligned at the call.
__attribute__((naked, target("arm"))) static int loop_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "push {r4-r8, lr}\n\t"
        "vpush {d8-d15}\n\t"
        "ldr r4, [r1]\n\t"
        "vmrs r7, fpscr\n\t"
        "bic r5, r7, #0x400000\n\t"
        "orr r5, r5, #0x800000\n\t"
        "vmsr fpscr, r5\n\t"
        "add r5, r4, #16\n\t"
        "add r6, r5, #64\n\t"
        "vldmia r5, {d0-d7}\n\t"
        "vldmia r6!, {d8-d15}\n\t"
        "vldmia r6, {d16-d31}\n\t"
        ".globl vm_boundary_begin\n\t"
        "vm_boundary_begin:\n\t"
        "1:\n\t"
        "mov r6, #100\n\t"
        "2:\n\t"
        "subs r6, r6, #1\n\t"
        "bne 2b\n\t"
        "ldr r0, [r4]\n\t"
        "add r0, r0, #1\n\t"
        "str r0, [r4]\n\t"
        "ldr r0, [r4, #8]\n\t"
        "mov r1, #1\n\t"
        "bl sceKernelPollSema\n\t"
        "ldr r0, [r4, #4]\n\t"
        "cmp r0, #0\n\t"
        "beq 1b\n\t"
        ".globl vm_boundary_end\n\t"
        "vm_boundary_end:\n\t"
        "vmsr fpscr, r7\n\t"
        "vpop {d8-d15}\n\t"
        "mov r0, #0\n\t"
        "pop {r4-r8, pc}\n\t");
}

// Reads W, which is suspended, into the sample
static void read_sample(SceUID w, SceUIntPtr stack_low, SceUIntPtr stack_high, Sample *s) {
    ThreadCpuRegisters cpu = { .size = sizeof(cpu) };
    ThreadVfpRegisters vfp = { .size = sizeof(vfp) };
    s->read = sceKernelGetThreadContextForVM(w, &cpu, &vfp);
    s->mode = cpu.cpsr & 0x1F;
    s->pc = cpu.reg[15];
    s->sp_on_stack = cpu.reg[13] > stack_low && cpu.reg[13] <= stack_high;
    s->fpscr = vfp.fpscr & FPSCR_CONTROL;
    s->kernel_view = s->fpscr != OWN_FPSCR;
    s->own_d8_d15 = 1;
    for (int d = 8; d < 16; d++) {
        const unsigned int low = vfp.reg[2 * d], high = vfp.reg[2 * d + 1];
        s->own_d8_d15 &= low == (unsigned int)shared.values[d] && high == (unsigned int)(shared.values[d] >> 32);
    }
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);
    for (int d = 0; d < 32; d++)
        shared.values[d] = (unsigned long long)(0xC1000000U + (unsigned int)d) << 32 | (0xC2000000U + (unsigned int)d);
    shared.sema = sceKernelCreateSema("empty", 0, 0, 1, NULL);

    const SceUID w = sceKernelCreateThread("w", loop_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(w, &info);
    const SceUIntPtr stack_low = (SceUIntPtr)info.stack;
    const SceUIntPtr stack_high = stack_low + info.stackSize;
    Shared *arg = &shared;
    sceKernelStartThread(w, sizeof(arg), &arg);
    TEST_WAIT_FOR(shared.counter > 0);

    int rounds = 0, caught = 0;
    while (rounds < MAX_ROUNDS && (rounds < MIN_ROUNDS || !caught)) {
        Sample *s = &samples[rounds++];
        s->suspend = sceKernelSuspendThreadForVM(w);
        const unsigned int stopped_at = shared.counter;
        read_sample(w, stack_low, stack_high, s);
        s->resume = sceKernelResumeThreadForVM(w);
        TEST_WAIT_FOR(shared.counter != stopped_at);
        s->moved = shared.counter != stopped_at;
        caught |= s->kernel_view;
    }
    shared.stop = 1;
    test_join_thread(w);
    sceKernelDeleteSema(shared.sema);

    // The stub's address has the Thumb bit set if it is Thumb code
    const SceUIntPtr stub = (SceUIntPtr)sceKernelPollSema & ~1U;
    const SceUIntPtr loop_low = (SceUIntPtr)vm_boundary_begin, loop_high = (SceUIntPtr)vm_boundary_end;
    // Counts of the samples that hold each fact. A kind with no sample holds every fact, so a
    // fact is printed as "all of them", which doesn't depend on how many samples each kind got.
    int suspends = 0, reads = 0, resumes = 0, moves = 0, user_mode = 0, sp_ok = 0, placed = 0, own_d8_d15 = 0;
    int kernel = 0, kernel_in_stub = 0, kernel_fpscr = 0, kernel_pcs = 0;
    static unsigned int pcs[MAX_ROUNDS];
    for (int i = 0; i < rounds; i++) {
        const Sample *s = &samples[i];
        const int in_loop = s->pc >= loop_low && s->pc < loop_high;
        const int in_stub = s->pc >= stub && s->pc < stub + STUB_SIZE;
        suspends += s->suspend == 1;
        reads += s->read == 0;
        resumes += s->resume == 0;
        moves += s->moved;
        user_mode += s->mode == 0x10;
        sp_ok += s->sp_on_stack;
        placed += in_loop || in_stub;
        own_d8_d15 += s->own_d8_d15;
        if (s->kernel_view) {
            kernel++;
            kernel_in_stub += in_stub;
            kernel_fpscr += s->fpscr == KERNEL_FPSCR;
            int seen = 0;
            for (int j = 0; j < kernel_pcs; j++)
                seen |= pcs[j] == s->pc;
            if (!seen)
                pcs[kernel_pcs++] = s->pc;
        }
    }

    PRINT("a sample with the kernel view was caught", caught);
    PRINT("all suspends returned 1", suspends == rounds);
    PRINT("all reads returned 0", reads == rounds);
    PRINT("all resumes returned 0", resumes == rounds);
    PRINT("the counter moved after every resume", moves == rounds);
    PRINT("all samples are in user mode", user_mode == rounds);
    PRINT("all samples have sp on W's stack", sp_ok == rounds);
    PRINT("all samples have the pc in the loop or in the stub", placed == rounds);
    PRINT("all samples have d8 to d15 as W set them", own_d8_d15 == rounds);
    PRINT("all samples with the kernel view have the pc in the stub", kernel_in_stub == kernel);
    PRINT("all samples with the kernel view have FPSCR control bits 0x03000000", kernel_fpscr == kernel);
    PRINT("all samples with the kernel view have the same pc", kernel_pcs <= 1);
    test_exit();
    return 0;
}
