// The callee-saved registers after a syscall that runs a callback. Before the call the caller sets
// r4 to r11 and d8 to d15 to markers, and the callback the syscall runs writes its own value to r4
// to r12 and to d0 to d31. The callback is naked, so it saves nothing, and whatever the thread
// holds afterwards is what the kernel put back. The output shows which markers came back, which
// registers hold the callback's value, whether sp is the same, and the FPSCR control bits. The
// masks count bits from the lowest register, so bit 0 is r4 or d8.
//
// Each case runs the callback from a different place: a direct call of the naked callback, which
// shows that its values are really written, a control with nothing pending, a
// sceKernelCheckCallback, a semaphore wait with callbacks and a 1 ms timeout, a delay with
// callbacks, and a nested case, where a callback run by a wait notifies the naked callback and
// checks again. The nested case goes last, since it is the one most likely to end a broken run.
// Each case creates its callbacks fresh, and the output also shows how many notifications are
// left on the naked callback, so a callback that didn't run shows up.
#include <stddef.h>

#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

// The markers in r4 to r11 are the ones thread_start_registers sets, and in d8 to d15 each is a
// different byte repeated
static const unsigned int r_markers[8] = { 0x44444444, 0x55555555, 0x66666666, 0x77777777,
                                          0x88888888, 0x99999999, 0xAAAAAAAA, 0xBBBBBBBB };
static const unsigned long long d_markers[8] = {
    0x0808080808080808ULL, 0x0909090909090909ULL, 0x0A0A0A0A0A0A0A0AULL, 0x0B0B0B0B0B0B0B0BULL,
    0x0C0C0C0C0C0C0C0CULL, 0x0D0D0D0D0D0D0D0DULL, 0x0E0E0E0E0E0E0E0EULL, 0x0F0F0F0F0F0F0F0FULL,
};

// The value the naked callback leaves in r4 to r12 and d0 to d31
#define CB_WORD 0xCBCBCBCB
#define CB_DOUBLE 0xCBCBCBCBCBCBCBCBULL

// One call under test. call_marked fills in the result and the registers after the call. The
// offsets are fixed in its asm, and the asserts below check them.
typedef struct Call {
    int (*fn)(int, int, void *);
    int a0, a1;
    void *a2;
    unsigned long long d_in[8];
    unsigned long long d_out[8];
    unsigned int result;
    unsigned int sp_before, sp_after;
    unsigned int r_out[8];
} Call;

_Static_assert(offsetof(Call, a0) == 4, "call_marked assumes this layout");
_Static_assert(offsetof(Call, d_in) == 16, "call_marked assumes this layout");
_Static_assert(offsetof(Call, d_out) == 80, "call_marked assumes this layout");
_Static_assert(offsetof(Call, result) == 144, "call_marked assumes this layout");
_Static_assert(offsetof(Call, sp_before) == 148, "call_marked assumes this layout");
_Static_assert(offsetof(Call, r_out) == 156, "call_marked assumes this layout");

// Loads r4 to r11 with markers and d8 to d15 from c->d_in, calls c->fn(a0, a1, a2) in the same
// block, then stores the result, sp, r4 to r11 and d8 to d15 into c. The caller's own r4 to r11 and
// d8 to d15 come back from the stack.
__attribute__((naked)) static void call_marked(Call *c) {
    (void)c;
    __asm__ volatile(
        "push {r4-r11, lr}\n\t"
        "vpush {d8-d15}\n\t"
        "push {r0}\n\t"
        "mov r1, sp\n\t"
        "str r1, [r0, #148]\n\t"
        "vldr d8, [r0, #16]\n\t"
        "vldr d9, [r0, #24]\n\t"
        "vldr d10, [r0, #32]\n\t"
        "vldr d11, [r0, #40]\n\t"
        "vldr d12, [r0, #48]\n\t"
        "vldr d13, [r0, #56]\n\t"
        "vldr d14, [r0, #64]\n\t"
        "vldr d15, [r0, #72]\n\t"
        "movw r4, #0x4444\n\t"
        "movt r4, #0x4444\n\t"
        "movw r5, #0x5555\n\t"
        "movt r5, #0x5555\n\t"
        "movw r6, #0x6666\n\t"
        "movt r6, #0x6666\n\t"
        "movw r7, #0x7777\n\t"
        "movt r7, #0x7777\n\t"
        "movw r8, #0x8888\n\t"
        "movt r8, #0x8888\n\t"
        "movw r9, #0x9999\n\t"
        "movt r9, #0x9999\n\t"
        "movw r10, #0xAAAA\n\t"
        "movt r10, #0xAAAA\n\t"
        "movw r11, #0xBBBB\n\t"
        "movt r11, #0xBBBB\n\t"
        "ldr r3, [r0, #0]\n\t"
        "ldr r2, [r0, #12]\n\t"
        "ldr r1, [r0, #8]\n\t"
        "ldr r0, [r0, #4]\n\t"
        "blx r3\n\t"
        "ldr r2, [sp]\n\t"
        "str r0, [r2, #144]\n\t"
        "mov r1, sp\n\t"
        "str r1, [r2, #152]\n\t"
        "vstr d8, [r2, #80]\n\t"
        "vstr d9, [r2, #88]\n\t"
        "vstr d10, [r2, #96]\n\t"
        "vstr d11, [r2, #104]\n\t"
        "vstr d12, [r2, #112]\n\t"
        "vstr d13, [r2, #120]\n\t"
        "vstr d14, [r2, #128]\n\t"
        "vstr d15, [r2, #136]\n\t"
        "str r4, [r2, #156]\n\t"
        "str r5, [r2, #160]\n\t"
        "str r6, [r2, #164]\n\t"
        "str r7, [r2, #168]\n\t"
        "str r8, [r2, #172]\n\t"
        "str r9, [r2, #176]\n\t"
        "str r10, [r2, #180]\n\t"
        "str r11, [r2, #184]\n\t"
        "pop {r0}\n\t"
        "vpop {d8-d15}\n\t"
        "pop {r4-r11, pc}\n\t");
}

// Writes the callback's value to r4 to r12 and d0 to d31, sets r0 to 0 and returns. It saves
// nothing, so a thread gets its own values back only if the kernel saves and restores them.
#define CB_D(n) "vmov d" #n ", r0, r0\n\t"
__attribute__((naked)) static int clobber_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "movw r0, #0xCBCB\n\t"
        "movt r0, #0xCBCB\n\t"
        "mov r1, r0\n\t"
        "mov r2, r0\n\t"
        "mov r3, r0\n\t"
        "mov r4, r0\n\t"
        "mov r5, r0\n\t"
        "mov r6, r0\n\t"
        "mov r7, r0\n\t"
        "mov r8, r0\n\t"
        "mov r9, r0\n\t"
        "mov r10, r0\n\t"
        "mov r11, r0\n\t"
        "mov r12, r0\n\t"
        CB_D(0) CB_D(1) CB_D(2) CB_D(3) CB_D(4) CB_D(5) CB_D(6) CB_D(7)
        CB_D(8) CB_D(9) CB_D(10) CB_D(11) CB_D(12) CB_D(13) CB_D(14) CB_D(15)
        CB_D(16) CB_D(17) CB_D(18) CB_D(19) CB_D(20) CB_D(21) CB_D(22) CB_D(23)
        CB_D(24) CB_D(25) CB_D(26) CB_D(27) CB_D(28) CB_D(29) CB_D(30) CB_D(31)
        "movs r0, #0\n\t"
        "bx lr\n\t");
}
#undef CB_D

static SceUID clobber_cb, outer_cb;

// Notifies the naked callback from inside this callback, then checks, so it runs nested
static int outer_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    sceKernelNotifyCallback(clobber_cb, 1);
    sceKernelCheckCallback();
    return 0;
}

// Which callback, if any, run_case notifies before the call
#define NOTIFY_NONE 0
#define NOTIFY_CLOBBER 1
#define NOTIFY_OUTER 2

static void set_call(Call *c, int (*fn)(int, int, void *), int a0, int a1, void *a2) {
    c->fn = fn;
    c->a0 = a0;
    c->a1 = a1;
    c->a2 = a2;
    for (int i = 0; i < 8; i++)
        c->d_in[i] = d_markers[i];
}

// Prints what the thread holds after the call, from the registers the call left in c
static void report(const char *name, const Call *c) {
    unsigned int r_kept = 0, r_cb = 0, d_kept = 0, d_cb = 0;
    for (int i = 0; i < 8; i++) {
        if (c->r_out[i] == r_markers[i])
            r_kept |= 1u << i;
        if (c->r_out[i] == CB_WORD)
            r_cb |= 1u << i;
        if (c->d_out[i] == d_markers[i])
            d_kept |= 1u << i;
        if (c->d_out[i] == CB_DOUBLE)
            d_cb |= 1u << i;
    }
    PRINTF(c->result, "%s: the call's result", name);
    PRINTF(sceKernelGetCallbackCount(clobber_cb), "%s: the callback's notifications left", name);
    PRINTF(r_kept, "%s: mask of r4 to r11 that kept their markers", name);
    PRINTF(r_cb, "%s: mask of r4 to r11 that hold the callback's value", name);
    PRINTF(d_kept, "%s: mask of d8 to d15 that kept their markers", name);
    PRINTF(d_cb, "%s: mask of d8 to d15 that hold the callback's value", name);
    PRINTF(c->sp_before == c->sp_after, "%s: sp is the same", name);
    PRINTF(get_fpscr() & FPSCR_CONTROL, "%s: the FPSCR control bits", name);
}

// Creates the callbacks, notifies the ones notify asks for, runs fn(a0, a1, a2) with the markers
// set and the rounding mode plus, then prints what the thread holds
static void run_case(const char *name, int notify, int (*fn)(int, int, void *), int a0, int a1, void *a2) {
    clobber_cb = sceKernelCreateCallback("clobber", 0, clobber_callback, NULL);
    outer_cb = sceKernelCreateCallback("outer", 0, outer_callback, NULL);
    if (notify == NOTIFY_CLOBBER)
        sceKernelNotifyCallback(clobber_cb, 1);
    else if (notify == NOTIFY_OUTER)
        sceKernelNotifyCallback(outer_cb, 1);

    Call c;
    set_call(&c, fn, a0, a1, a2);
    set_rmode(RMODE_PLUS);
    call_marked(&c);
    report(name, &c);

    sceKernelDeleteCallback(outer_cb);
    sceKernelDeleteCallback(clobber_cb);
}

#define CLOBBER_DIRECT ((int (*)(int, int, void *))(void *)clobber_callback)
#define CHECK ((int (*)(int, int, void *))(void *)sceKernelCheckCallback)
#define WAIT_SEMA_CB ((int (*)(int, int, void *))(void *)sceKernelWaitSemaCB)
#define DELAY_CB ((int (*)(int, int, void *))(void *)sceKernelDelayThreadCB)

int main(void) {
    run_case("direct call", NOTIFY_NONE, CLOBBER_DIRECT, 0, 0, NULL);
    run_case("control", NOTIFY_NONE, CHECK, 0, 0, NULL);
    run_case("check", NOTIFY_CLOBBER, CHECK, 0, 0, NULL);

    SceUID sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    SceUInt timeout = 1000;
    run_case("CB wait", NOTIFY_CLOBBER, WAIT_SEMA_CB, sema, 1, &timeout);
    run_case("CB delay", NOTIFY_CLOBBER, DELAY_CB, 1000, 0, NULL);
    timeout = 1000;
    run_case("nested", NOTIFY_OUTER, WAIT_SEMA_CB, sema, 1, &timeout);
    sceKernelDeleteSema(sema);

    test_exit();
    return 0;
}
