// What d0 to d31 hold after sceKernelCheckCallback runs a callback, when the caller set all of them
// to markers before the call and the callback set all of them to markers of its own. One asm block
// loads the caller's markers, calls the import and stores the registers again, so no compiler code
// touches them in between. The callback is a naked function that loads its markers into d0 to d31
// and returns 0. The test runs on one thread and has two cases: a check with the callback notified,
// and a check with nothing notified, which runs no callback.
//
// For each case it prints the call's result, which d registers hold the caller's marker, which hold
// the callback's marker, which of the others share the value of the first one that holds neither
// marker, that value and which registers hold none of these.
//
// On a PS Vita TV on firmware 3.65 the check that runs the callback returns 1 and leaves d8 to d15
// with the caller's markers, d2 and d3 with the callback's markers, and every other d register
// with 0x7FF8DEAD7F80DEAD. The check with nothing notified returns 0 and keeps all 32 registers.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

static unsigned long long markers[32], callback_markers[32], after[32];

// The import itself, so nothing compiled here runs between the call and the syscall
#define CHECK ((int (*)(void))(void *)sceKernelCheckCallback)

// The callback: loads callback_markers into d0 to d31 and returns 0
__attribute__((naked)) static int marking_callback(__attribute__((unused)) int notify_id, __attribute__((unused)) int count,
                                                    __attribute__((unused)) int arg, __attribute__((unused)) void *common) {
    __asm__ volatile(
        "movw r1, #:lower16:callback_markers\n\t"
        "movt r1, #:upper16:callback_markers\n\t"
        "vldmia r1!, {d0-d15}\n\t"
        "vldmia r1, {d16-d31}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// Loads d0 to d31 from markers, calls sceKernelCheckCallback and stores d0 to d31 to after
static int check_with_all_d(void) {
    register int r0 __asm__("r0");
    int (*fn)(void) = CHECK;
    __asm__ volatile(
        "vldmia %[in0], {d0-d15}\n\t"
        "vldmia %[in16], {d16-d31}\n\t"
        "blx %[fn]\n\t"
        "vstmia %[out0], {d0-d15}\n\t"
        "vstmia %[out16], {d16-d31}\n\t"
        : "=&r"(r0)
        : [in0] "r"(&markers[0]), [in16] "r"(&markers[16]), [out0] "r"(&after[0]), [out16] "r"(&after[16]), [fn] "r"(fn)
        : "r1", "r2", "r3", "r12", "lr", "memory", "cc", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
          "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
          "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
    return r0;
}

static void run(const char *name) {
    const int result = check_with_all_d();
    PRINTF(result, "%s: the result", name);
    // The value of the first register with neither marker, and the registers that share it
    unsigned long long other = 0;
    unsigned int caller = 0, callback = 0, shared = 0, none = 0;
    int others = 0;
    for (int d = 0; d < 32; d++) {
        if (after[d] == markers[d]) {
            caller |= 1U << d;
        } else if (after[d] == callback_markers[d]) {
            callback |= 1U << d;
        } else if (!others || after[d] == other) {
            other = after[d];
            shared |= 1U << d;
            others++;
        } else {
            none |= 1U << d;
        }
    }
    PRINTF(caller, "%s: the d registers with the caller's marker, one bit each", name);
    PRINTF(callback, "%s: the d registers with the callback's marker", name);
    PRINTF(shared, "%s: the d registers that share another value", name);
    PRINTF((unsigned int)(other >> 32), "%s: that value's high word", name);
    PRINTF((unsigned int)other, "%s: that value's low word", name);
    PRINTF(none, "%s: the d registers with none of these", name);
}

int main(void) {
    for (int d = 0; d < 32; d++) {
        markers[d] = (unsigned long long)(0xC1000000U + (unsigned int)d) << 32 | (0xC2000000U + (unsigned int)d);
        callback_markers[d] = (unsigned long long)(0xCB000000U + (unsigned int)d) << 32 | (0xCC000000U + (unsigned int)d);
    }

    const SceUID cb = sceKernelCreateCallback("vfp", 0, marking_callback, NULL);
    PRINT("create the callback", test_created(cb));
    PRINT("notify it", sceKernelNotifyCallback(cb, 1));
    run("a check that runs the callback");
    run("a check with nothing notified");
    PRINT("delete the callback", sceKernelDeleteCallback(cb));
    test_exit();
    return 0;
}
