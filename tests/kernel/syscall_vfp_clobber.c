// What d0 to d31 hold after a syscall when the caller set all of them to markers before it. The
// ARM calling convention only has a called function keep d8 to d15, so the others may change.
// syscall_callee_saved only reads d8 to d15 and thread_nouse_vfp reads d8 and d16 after a wait, so
// this test reads every register after five calls: a delay of 1 ms, a semaphore wait that times
// out after 1 ms, then a poll with a count there, a poll with none there and sceKernelGetThreadId.
// One asm block loads the markers, calls the import and stores the registers again, so no compiler
// code touches them in between. The test runs on one thread.
//
// For each call it prints which d registers kept their marker, which of the others share the value
// of the first one that didn't, that value and which registers hold neither.
//
// On a PS Vita TV on firmware 3.65 the delay and the semaphore wait both change d0, d1, d4 to d7
// and d16 to d31, which all hold 0x7FF8DEAD7F80DEAD afterwards, and keep d2, d3 and d8 to d15. Both
// polls and sceKernelGetThreadId keep all 32 registers. The delay and the first poll return 0, the
// wait returns 0x80028005 and the second poll returns 0x80028102.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static unsigned long long markers[32], after[32];
static SceUID sema;
static SceUInt timeout = 1000;

// The imports themselves, so nothing compiled here runs between the call and the syscall
#define DELAY ((int (*)(int, int, void *))(void *)sceKernelDelayThread)
#define WAIT ((int (*)(int, int, void *))(void *)sceKernelWaitSema)
#define POLL ((int (*)(int, int, void *))(void *)sceKernelPollSema)
#define GET_ID ((int (*)(int, int, void *))(void *)sceKernelGetThreadId)

// Loads d0 to d31 from markers, calls fn(a0, a1, a2) and stores d0 to d31 to after
static int call_with_all_d(int (*fn)(int, int, void *), int a0, int a1, void *a2) {
    register int r0 __asm__("r0") = a0;
    register int r1 __asm__("r1") = a1;
    register void *r2 __asm__("r2") = a2;
    __asm__ volatile(
        "vldmia %[in0], {d0-d15}\n\t"
        "vldmia %[in16], {d16-d31}\n\t"
        "blx %[fn]\n\t"
        "vstmia %[out0], {d0-d15}\n\t"
        "vstmia %[out16], {d16-d31}\n\t"
        : "+r"(r0), "+r"(r1), "+r"(r2)
        : [in0] "r"(&markers[0]), [in16] "r"(&markers[16]), [out0] "r"(&after[0]), [out16] "r"(&after[16]), [fn] "r"(fn)
        : "r3", "r12", "lr", "memory", "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
          "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
          "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
    return r0;
}

static void run(const char *name, int (*fn)(int, int, void *), int a0, int a1, void *a2, int show_result) {
    const int result = call_with_all_d(fn, a0, a1, a2);
    if (show_result)
        PRINTF(result, "%s: the result", name);
    // The value of the first register without its marker, and the registers that share it
    unsigned long long other = 0;
    unsigned int kept = 0, shared = 0, neither = 0;
    int others = 0;
    for (int d = 0; d < 32; d++) {
        if (after[d] == markers[d]) {
            kept |= 1U << d;
        } else if (!others || after[d] == other) {
            other = after[d];
            shared |= 1U << d;
            others++;
        } else {
            neither |= 1U << d;
        }
    }
    PRINTF(kept, "%s: the d registers that kept their marker, one bit each", name);
    PRINTF(shared, "%s: the d registers that share the other value", name);
    PRINTF((unsigned int)(other >> 32), "%s: that value's high word", name);
    PRINTF((unsigned int)other, "%s: that value's low word", name);
    PRINTF(neither, "%s: the d registers with neither", name);
}

int main(void) {
    for (int d = 0; d < 32; d++)
        markers[d] = (unsigned long long)(0xC1000000U + (unsigned int)d) << 32 | (0xC2000000U + (unsigned int)d);

    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    run("sceKernelDelayThread(1000)", DELAY, 1000, 0, NULL, 1);
    run("sceKernelWaitSema timed out after 1 ms", WAIT, sema, 1, &timeout, 1);
    sceKernelSignalSema(sema, 1);
    run("sceKernelPollSema with a count there", POLL, sema, 1, NULL, 1);
    run("sceKernelPollSema with no count there", POLL, sema, 1, NULL, 1);
    // A UID, which differs between runs
    run("sceKernelGetThreadId", GET_ID, 0, 0, NULL, 0);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
