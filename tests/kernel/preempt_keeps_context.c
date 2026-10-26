// Whether a thread that the kernel preempts keeps its whole context. The low thread L, priority
// 0xA0 on core 1, loads markers into r0 to r11, d0 to d31, TPIDRURW and FPSCR, sets the flags
// NZCV, Q and GE, and spins in a loop of loads and branches until the main thread sets a stop flag.
// The loop changes no register, flag or stack word. The high thread H, priority 0x80 on core 1,
// sets the same registers, flags and FPSCR to other values and ends, so the kernel preempts L
// while it spins. Two variants:
// - A: L spins, then the main thread on core 2 starts H, which preempts L as it starts.
// - B: H starts first and sleeps for a second, then L starts and spins, and H wakes from its delay
//   while L still spins, which preempts L.
// Once H has ended, the main thread sets the stop flag, and L stores its state after the loop.
// Each variant prints whether H ran during L's loop, and if not, a skip marker in place of the
// checks. Otherwise it prints the mask of r0 to r11 that L keeps, the flags L set and whether
// they are kept, the mask of d0 to d31 that L keeps, and whether FPSCR, TPIDRURW, TPIDRURO and sp
// are the same as before the loop. For FPSCR and TPIDRURW it also prints whether they read back as
// L set them before the loop. The threads are ordered by L never making a syscall, so no sleep
// waits for a thread. The main thread only waits for L's spin flag and for H, with finite timeouts.
#include <psp2/kernel/threadmgr.h>

#include <stddef.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY 0xA0
#define HIGH_PRIORITY 0x80
// Keeps the flags of NZCVQ and GE in APSR
#define APSR_CHECKED 0xF80F0000U
// The flags of NZCVQ and GE that L sets, and that H changes to other ones
#define FPSCR_LOW 0x83800002U
#define FPSCR_HIGH 0x40400080U
#define TPIDRURW_LOW 0x1A2B3C4DU
#define TPIDRURW_HIGH 0x5E6F7081U

// The values a thread sets, read by LOAD_VALUES at the offsets the static asserts check
typedef struct Values {
    unsigned int gpr[12];
    unsigned long long vfp[32];
    unsigned int fpscr;
    unsigned int tpidrurw;
} Values;

_Static_assert(offsetof(Values, vfp) == 48, "Values.vfp is read at offset 48");
_Static_assert(offsetof(Values, fpscr) == 304, "Values.fpscr is read at offset 304");
_Static_assert(offsetof(Values, tpidrurw) == 308, "Values.tpidrurw is read at offset 308");

// What L stores after its loop. The before fields are its state when the loop started, read from
// its stack frame. The offsets from the first store are read by the assembly, as the static asserts
// check.
typedef struct State {
    unsigned int gpr[12];
    unsigned long long vfp[32];
    unsigned int fpscr, apsr, tpidrurw, tpidruro, sp;
    unsigned int before_sp, before_apsr, before_fpscr, before_tpidrurw, before_tpidruro;
} State;

_Static_assert(offsetof(State, fpscr) == 304, "State.fpscr is stored at offset 304");
_Static_assert(offsetof(State, before_sp) == 324, "State.before_sp is stored at offset 324");

static Values low_values __attribute__((used)), high_values __attribute__((used));
static State low_state __attribute__((used));
static volatile int spinning __attribute__((used)), stop_flag __attribute__((used));
static volatile int high_ran __attribute__((used)), high_saw_spin __attribute__((used));

// Loads FPSCR and TPIDRURW, then r0 to r11 and d0 to d31, from the Values at table. Leaves r12
// pointing past the table. Changes no flags, and uses r0 and r12 as scratch before they are loaded.
#define LOAD_VALUES(table)                                  \
    "movw r12, :lower16:" #table "\n\t"                     \
    "movt r12, :upper16:" #table "\n\t"                     \
    "ldr r0, [r12, #304]\n\t"                               \
    "vmsr fpscr, r0\n\t"                                    \
    "ldr r0, [r12, #308]\n\t"                               \
    "mcr p15, 0, r0, c13, c0, 2\n\t"                        \
    "ldmia r12, {r0-r11}\n\t"                               \
    "add.w r12, r12, #48\n\t"                               \
    "vldmia r12!, {d0-d15}\n\t"                             \
    "vldmia r12!, {d16-d31}\n\t"

// Sets the high thread's values, its flags last, then records that it ran and whether L was
// spinning when it did, and returns.
#define HIGH_BODY                                           \
    LOAD_VALUES(high_values)                                \
    "movw r12, #0\n\t"                                      \
    "msr apsr_nzcvq, r12\n\t"                               \
    "movw r12, #0x0101\n\t"                                 \
    "movt r12, #0x0101\n\t"                                 \
    "uadd8 r12, r12, r12\n\t"                               \
    "cmp r12, r12\n\t"                                      \
    "movw r12, :lower16:spinning\n\t"                       \
    "movt r12, :upper16:spinning\n\t"                       \
    "ldr r0, [r12]\n\t"                                     \
    "movw r12, :lower16:high_saw_spin\n\t"                  \
    "movt r12, :upper16:high_saw_spin\n\t"                  \
    "str r0, [r12]\n\t"                                     \
    "movw r0, #1\n\t"                                       \
    "movw r12, :lower16:high_ran\n\t"                       \
    "movt r12, :upper16:high_ran\n\t"                       \
    "str r0, [r12]\n\t"                                     \
    "bx lr\n\t"

// Sleeps for a second, so that H wakes while L spins. Called from the assembly of H.
void high_delay(void);
void high_delay(void) {
    sceKernelDelayThread(1000000);
}

// The low thread. It sets its state, spins while stop_flag is 0, and stores its state. The loop is
// tbb on the flag: index 0 branches back to the loop, index 1 leaves it. It only loads and
// branches, so no flag changes in it.
__attribute__((naked)) static int low_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        LOAD_VALUES(low_values)
        // NZCVQ and GE: a uadd8 with carries in bytes 1 and 3 sets GE to 1010, a usat that
        // saturates sets Q, and a cmp of 255 with 256 sets N
        "movw r12, #0x8000\n\t"
        "movt r12, #0x8000\n\t"
        "uadd8 r12, r12, r12\n\t"
        "movw r12, #0x1000\n\t"
        "usat r12, #8, r12\n\t"
        "cmp r12, #0x100\n\t"
        // The frame holds sp, APSR, FPSCR, TPIDRURW, TPIDRURO and the address of stop_flag
        "sub sp, sp, #32\n\t"
        "mov r12, sp\n\t"
        "str r12, [sp, #0]\n\t"
        "mrs r12, apsr\n\t"
        "str r12, [sp, #4]\n\t"
        "vmrs r12, fpscr\n\t"
        "str r12, [sp, #8]\n\t"
        "mrc p15, 0, r12, c13, c0, 2\n\t"
        "str r12, [sp, #12]\n\t"
        "mrc p15, 0, r12, c13, c0, 3\n\t"
        "str r12, [sp, #16]\n\t"
        "movw r12, :lower16:stop_flag\n\t"
        "movt r12, :upper16:stop_flag\n\t"
        "str r12, [sp, #20]\n\t"
        // The spinning flag gets its own address, which is not 0, since no register is free for a 1
        "movw r12, :lower16:spinning\n\t"
        "movt r12, :upper16:spinning\n\t"
        "str r12, [r12]\n\t"
        "1:\n\t"
        "ldr r12, [sp, #20]\n\t"
        "ldr r12, [r12]\n\t"
        "tbb [pc, r12]\n\t"
        "4:\n\t"
        ".byte (2f - 4b) / 2\n\t"
        ".byte (3f - 4b) / 2\n\t"
        "2:\n\t"
        "b 1b\n\t"
        "3:\n\t"
        // The state after the loop: r0 to r11 and d0 to d31 first, then the rest through r0
        "movw r12, :lower16:low_state\n\t"
        "movt r12, :upper16:low_state\n\t"
        "stmia r12!, {r0-r11}\n\t"
        "vstmia r12!, {d0-d15}\n\t"
        "vstmia r12!, {d16-d31}\n\t"
        "vmrs r0, fpscr\n\t"
        "str r0, [r12, #0]\n\t"
        "mrs r0, apsr\n\t"
        "str r0, [r12, #4]\n\t"
        "mrc p15, 0, r0, c13, c0, 2\n\t"
        "str r0, [r12, #8]\n\t"
        "mrc p15, 0, r0, c13, c0, 3\n\t"
        "str r0, [r12, #12]\n\t"
        "mov r0, sp\n\t"
        "str r0, [r12, #16]\n\t"
        "ldr r0, [sp, #0]\n\t"
        "str r0, [r12, #20]\n\t"
        "ldr r0, [sp, #4]\n\t"
        "str r0, [r12, #24]\n\t"
        "ldr r0, [sp, #8]\n\t"
        "str r0, [r12, #28]\n\t"
        "ldr r0, [sp, #12]\n\t"
        "str r0, [r12, #32]\n\t"
        "ldr r0, [sp, #16]\n\t"
        "str r0, [r12, #36]\n\t"
        "add sp, sp, #32\n\t"
        "bx lr\n\t");
}

// The high thread for variant A, which sets its values at once
__attribute__((naked)) static int high_now_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(HIGH_BODY);
}

// The high thread for variant B, which first sleeps for its delay
__attribute__((naked)) static int high_wake_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "bl high_delay\n\t"
        "pop {r4, lr}\n\t"
        HIGH_BODY);
}

static unsigned int mask_of_gpr(void) {
    unsigned int kept = 0;
    for (int i = 0; i < 12; i++) {
        if (low_state.gpr[i] == low_values.gpr[i])
            kept |= 1U << i;
    }
    return kept;
}

static unsigned int mask_of_vfp(void) {
    unsigned int kept = 0;
    for (int d = 0; d < 32; d++) {
        if (low_state.vfp[d] == low_values.vfp[d])
            kept |= 1U << d;
    }
    return kept;
}

static void print_state(const char *name) {
    const int ran = high_ran && high_saw_spin;
    PRINTF(ran, "%s: H ran during L's loop", name);
    if (!ran) {
        PRINTF(0xDEAD, "%s: the checks are skipped", name);
        return;
    }
    PRINTF(mask_of_gpr(), "%s: the r0 to r11 kept, one bit each", name);
    PRINTF(low_state.before_apsr & APSR_CHECKED, "%s: the flags L set, NZCVQ and GE", name);
    PRINTF((low_state.apsr & APSR_CHECKED) == (low_state.before_apsr & APSR_CHECKED), "%s: the flags kept", name);
    PRINTF(mask_of_vfp(), "%s: the d0 to d31 kept, one bit each", name);
    PRINTF(low_state.before_fpscr == low_values.fpscr, "%s: FPSCR as L set it", name);
    PRINTF(low_state.fpscr == low_state.before_fpscr, "%s: FPSCR the same as before the loop", name);
    PRINTF(low_state.before_tpidrurw == low_values.tpidrurw, "%s: TPIDRURW as L set it", name);
    PRINTF(low_state.tpidrurw == low_state.before_tpidrurw, "%s: TPIDRURW the same as before the loop", name);
    PRINTF(low_state.tpidruro == low_state.before_tpidruro, "%s: TPIDRURO the same as before the loop", name);
    PRINTF(low_state.sp == low_state.before_sp, "%s: sp the same as before the loop", name);
}

// Runs L and H in one variant. With before_low, H starts first and sleeps, then L starts.
// Otherwise L starts first, and H starts once L spins.
static void run_case(const char *name, SceKernelThreadEntry high_entry, int before_low) {
    spinning = stop_flag = high_ran = high_saw_spin = 0;
    low_state = (State){ 0 };
    SceUInt timeout = TEST_WAIT_US;
    const SceUID low = sceKernelCreateThread("low", low_entry, LOW_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID high = sceKernelCreateThread("high", high_entry, HIGH_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (before_low) {
        sceKernelStartThread(high, 0, NULL);
        test_wait_until_waiting(high);
        sceKernelStartThread(low, 0, NULL);
        TEST_WAIT_FOR(spinning);
    } else {
        sceKernelStartThread(low, 0, NULL);
        TEST_WAIT_FOR(spinning);
        sceKernelStartThread(high, 0, NULL);
    }
    sceKernelWaitThreadEnd(high, NULL, &timeout);
    stop_flag = 1;
    sceKernelWaitThreadEnd(low, NULL, &timeout);
    sceKernelDeleteThread(high);
    sceKernelDeleteThread(low);
    print_state(name);
}

int main(void) {
    for (int i = 0; i < 12; i++) {
        low_values.gpr[i] = 0x10000000U + (unsigned int)i;
        high_values.gpr[i] = 0x20000000U + (unsigned int)i;
    }
    for (int d = 0; d < 32; d++) {
        low_values.vfp[d] = (unsigned long long)(0xD1000000U + (unsigned int)d) << 32 | (0xD2000000U + (unsigned int)d);
        high_values.vfp[d] = (unsigned long long)(0xE1000000U + (unsigned int)d) << 32 | (0xE2000000U + (unsigned int)d);
    }
    low_values.fpscr = FPSCR_LOW;
    high_values.fpscr = FPSCR_HIGH;
    low_values.tpidrurw = TPIDRURW_LOW;
    high_values.tpidrurw = TPIDRURW_HIGH;

    // The main thread moves to core 2, the threads L and H are on core 1
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    sceKernelDelayThread(1000);

    run_case("variant A, H started by the main thread", high_now_entry, 0);
    run_case("variant B, H woken by its delay", high_wake_entry, 1);

    test_exit();
    return 0;
}
