// What d0 to d31 hold in a thread after the call that woke a higher priority thread on its own
// core, when the thread set all of them to markers before the call. T runs at 0x80 and H1 and H2 at
// 0x60, all on core 0. H1 waits on a semaphore and H2 waits on a simple event, and each blocks again
// at once after a wake. One asm block in T loads the markers, calls the import and stores the
// registers again. The cases are a semaphore signal that wakes H1, a simple event set that wakes
// H2, a signal of a semaphore nobody waits on, a set of a simple event nobody waits on, and a set
// that wakes H3, which waits on its own simple event with no timeout (H1 and H2 wait with one). For each it prints which d registers kept their
// marker, one bit each, and whether the others all hold one value, with its two words.
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define T_PRIORITY 0x80
#define H_PRIORITY 0x60

static unsigned long long markers[32], after[5][32];
static SceUID wake_sema, wake_event, idle_sema, idle_event, plain_event, go;
static volatile int woke1, woke2, woke3, h_quit;
static int results[5];

static int h1_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!h_quit) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        if (sceKernelWaitSema(wake_sema, 1, &timeout) == 0 && !h_quit)
            woke1++;
    }
    return 0;
}

static int h2_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!h_quit) {
        SceUInt timeout = 3 * TEST_WAIT_US;
        SceUInt32 pattern;
        if (sceKernelWaitEvent(wake_event, EVENT_BIT_A, &pattern, NULL, &timeout) == 0 && !h_quit)
            woke2++;
    }
    return 0;
}

static int h3_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!h_quit) {
        SceUInt32 pattern;
        if (sceKernelWaitEvent(plain_event, EVENT_BIT_A, &pattern, NULL, NULL) == 0 && !h_quit)
            woke3++;
    }
    return 0;
}

// Loads d0 to d31 from markers, calls fn(a0, a1, a2, a3) and stores d0 to d31 to out
static int call_with_all_d(int (*fn)(int, int, int, int), int a0, int a1, int a2, int a3, unsigned long long *out) {
    register int r0 __asm__("r0") = a0;
    register int r1 __asm__("r1") = a1;
    register int r2 __asm__("r2") = a2;
    register int r3 __asm__("r3") = a3;
    __asm__ volatile(
        "vldmia %[in0], {d0-d15}\n\t"
        "vldmia %[in16], {d16-d31}\n\t"
        "blx %[fn]\n\t"
        "vstmia %[out0], {d0-d15}\n\t"
        "vstmia %[out16], {d16-d31}\n\t"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : [in0] "r"(&markers[0]), [in16] "r"(&markers[16]), [out0] "r"(&out[0]), [out16] "r"(&out[16]), [fn] "r"(fn)
        : "r12", "lr", "memory", "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
          "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
          "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
    return r0;
}

#define SIGNAL ((int (*)(int, int, int, int))(void *)sceKernelSignalSema)
#define SET_EVENT ((int (*)(int, int, int, int))(void *)sceKernelSetEvent)

static int t_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceKernelWaitSema(go, 1, NULL);
    results[0] = call_with_all_d(SIGNAL, wake_sema, 1, 0, 0, after[0]);
    results[1] = call_with_all_d(SET_EVENT, wake_event, EVENT_BIT_A, 0, 0, after[1]);
    results[2] = call_with_all_d(SIGNAL, idle_sema, 1, 0, 0, after[2]);
    results[3] = call_with_all_d(SET_EVENT, idle_event, EVENT_BIT_A, 0, 0, after[3]);
    results[4] = call_with_all_d(SET_EVENT, plain_event, EVENT_BIT_A, 0, 0, after[4]);
    return 0;
}

static void print_case(const char *name, int result, const unsigned long long *regs) {
    unsigned long long other = 0;
    unsigned int kept = 0, same = 1;
    int others = 0;
    for (int d = 0; d < 32; d++) {
        if (regs[d] == markers[d]) {
            kept |= 1U << d;
        } else if (!others) {
            other = regs[d];
            others = 1;
        } else if (regs[d] != other) {
            same = 0;
        }
    }
    PRINTF(result, "%s: the result", name);
    PRINTF(kept, "%s: the d registers that kept their marker, one bit each", name);
    PRINTF(same, "%s: the other registers all hold one value", name);
    PRINTF((unsigned int)(other >> 32), "%s: that value's high word", name);
    PRINTF((unsigned int)other, "%s: that value's low word", name);
}

int main(void) {
    for (int d = 0; d < 32; d++)
        markers[d] = (unsigned long long)(0xC1000000U + (unsigned int)d) << 32 | (0xC2000000U + (unsigned int)d);
    const int orig_mask = sceKernelGetThreadCpuAffinityMask(0);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    TEST_WAIT_FOR(sceKernelGetCpuId() == 2);

    wake_sema = sceKernelCreateSema("wake", 0, 0, 1, NULL);
    idle_sema = sceKernelCreateSema("idle", 0, 0, 1, NULL);
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    wake_event = sceKernelCreateSimpleEvent("wake", EVENT_ATTR_AUTO_RESET, 0, NULL);
    idle_event = sceKernelCreateSimpleEvent("idle", EVENT_ATTR_AUTO_RESET, 0, NULL);
    plain_event = sceKernelCreateSimpleEvent("plain", EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID h1 = sceKernelCreateThread("h1", h1_entry, H_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID h2 = sceKernelCreateThread("h2", h2_entry, H_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID h3 = sceKernelCreateThread("h3", h3_entry, H_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    const SceUID t = sceKernelCreateThread("t", t_entry, T_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(h1, 0, NULL);
    sceKernelStartThread(h2, 0, NULL);
    sceKernelStartThread(h3, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(h1, WAIT_TYPE_SEMA, wake_sema) && test_in_wait(h2, WAIT_TYPE_EVENT, wake_event) && test_in_wait(h3, WAIT_TYPE_EVENT, plain_event));
    sceKernelStartThread(t, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(t, WAIT_TYPE_SEMA, go));
    sceKernelSignalSema(go, 1);
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(t, NULL, &timeout);

    TEST_WAIT_FOR(woke1 == 1 && woke2 == 1 && woke3 == 1);
    PRINT("H1 woke once", woke1 == 1);
    PRINT("H2 woke once", woke2 == 1);
    PRINT("H3 woke once", woke3 == 1);
    print_case("a semaphore signal that wakes a better thread", results[0], after[0]);
    print_case("an event set that wakes a better thread", results[1], after[1]);
    print_case("a semaphore signal that wakes nobody", results[2], after[2]);
    print_case("an event set that wakes nobody", results[3], after[3]);
    print_case("an event set that wakes a better thread waiting with no timeout", results[4], after[4]);

    h_quit = 1;
    sceKernelSignalSema(wake_sema, 1);
    sceKernelSetEvent(wake_event, EVENT_BIT_A, 0);
    sceKernelSetEvent(plain_event, EVENT_BIT_A, 0);
    timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(h1, NULL, &timeout);
    sceKernelWaitThreadEnd(h2, NULL, &timeout);
    sceKernelWaitThreadEnd(h3, NULL, &timeout);
    sceKernelDeleteThread(h1);
    sceKernelDeleteThread(h2);
    sceKernelDeleteThread(h3);
    sceKernelDeleteThread(t);
    sceKernelDeleteSema(wake_sema);
    sceKernelDeleteSema(idle_sema);
    sceKernelDeleteSema(go);
    sceKernelDeleteSimpleEvent(wake_event);
    sceKernelDeleteSimpleEvent(idle_event);
    sceKernelDeleteSimpleEvent(plain_event);
    sceKernelChangeThreadCpuAffinityMask(0, orig_mask);
    test_exit();
    return 0;
}
