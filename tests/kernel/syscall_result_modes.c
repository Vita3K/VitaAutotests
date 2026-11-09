// What r0 to r3 and r12 hold after thread manager syscalls that return 64 bits, a pointer or an
// error, when r1, r2, r3 and r12 held marker values before the call. The calls are made through
// the imports themselves, so nothing compiled here runs between the call and the syscall:
// - sceKernelGetSystemTimeWide, whose r1:r0 is compared with two plain calls made around it
// - sceKernelGetTimerBaseWide with a UID that names nothing
// - sceKernelGetThreadTLSAddr with its own thread and key 7, compared with a plain call
// - sceKernelRegisterCallbackToEvent with two UIDs that name nothing
// The test prints only comparisons and fixed values, never the time or an address.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x12345
#define UNKNOWN_UID_2 0x12347

#define AS_CALL(fn) ((int (*)(int, int))(void *)(fn))

typedef struct Call {
    int (*fn)(int, int);
    int a0, a1, one_argument;
    unsigned int out[5];
} Call;

// Calls fn(a0, a1) with r2, r3 and r12, and r1 if fn takes one argument, set to markers, and keeps
// r0 to r3 and r12 as fn left them
static void call_marked(Call *call) {
    __asm__ volatile(
        "ldr r0, [%[c], #4]\n\t"
        "ldr r1, [%[c], #8]\n\t"
        "ldr r3, [%[c], #12]\n\t"
        "cmp r3, #0\n\t"
        "beq 1f\n\t"
        "movw r1, #0x1111\n\t"
        "movt r1, #0x1111\n\t"
        "1:\n\t"
        "movw r2, #0x2222\n\t"
        "movt r2, #0x2222\n\t"
        "movw r3, #0x3333\n\t"
        "movt r3, #0x3333\n\t"
        "movw r12, #0xCCCC\n\t"
        "movt r12, #0xCCCC\n\t"
        "ldr lr, [%[c]]\n\t"
        "blx lr\n\t"
        "str r0, [%[c], #16]\n\t"
        "str r1, [%[c], #20]\n\t"
        "str r2, [%[c], #24]\n\t"
        "str r3, [%[c], #28]\n\t"
        "str r12, [%[c], #32]\n\t"
        :
        : [c] "r"(call)
        : "r0", "r1", "r2", "r3", "r12", "lr", "memory", "cc");
}

// Prints the registers from first to r12 as numbers
static void print_from(const char *name, const unsigned int out[5], int first) {
    static const char *const regs[5] = { "r0", "r1", "r2", "r3", "r12" };
    for (int i = first; i < 5; i++)
        PRINTF(out[i], "%s: %s after it", name, regs[i]);
}

static unsigned long long wide(const unsigned int out[5]) {
    return ((unsigned long long)out[1] << 32) | out[0];
}

int main(void) {
    const unsigned long long before = (unsigned long long)sceKernelGetSystemTimeWide();
    Call time = { AS_CALL(sceKernelGetSystemTimeWide), 0, 0, 1, { 0 } };
    call_marked(&time);
    const unsigned long long after = (unsigned long long)sceKernelGetSystemTimeWide();
    const char *name = "sceKernelGetSystemTimeWide";
    PRINTF(before <= wide(time.out) && wide(time.out) <= after, "%s: r1:r0 lies between two plain calls around it", name);
    PRINTF(time.out[1] == 0xDEADBEEF, "%s: r1 is 0xDEADBEEF", name);
    PRINTF(time.out[1] == 0x11111111, "%s: r1 still holds its marker", name);
    print_from(name, time.out, 2);

    Call base = { AS_CALL(sceKernelGetTimerBaseWide), UNKNOWN_UID, 0, 1, { 0 } };
    call_marked(&base);
    print_from("sceKernelGetTimerBaseWide(an unknown UID)", base.out, 0);

    const SceUID self = sceKernelGetThreadId();
    Call tls = { AS_CALL(sceKernelGetThreadTLSAddr), self, 7, 0, { 0 } };
    call_marked(&tls);
    const unsigned int tls_plain = (unsigned int)sceKernelGetThreadTLSAddr(self, 7);
    name = "sceKernelGetThreadTLSAddr(its own thread, 7)";
    PRINTF(tls.out[0] == tls_plain, "%s: r0 is the plain call's result", name);
    PRINTF(tls.out[0] != 0, "%s: r0 is not 0", name);
    print_from(name, tls.out, 1);

    Call reg = { AS_CALL(sceKernelRegisterCallbackToEvent), UNKNOWN_UID, UNKNOWN_UID_2, 0, { 0 } };
    call_marked(&reg);
    print_from("sceKernelRegisterCallbackToEvent(unknown, unknown)", reg.out, 0);

    test_exit();
    return 0;
}
