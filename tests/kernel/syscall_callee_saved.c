// The callee-saved registers and the rounding mode after syscalls of several kinds. Before each
// call the thread sets r4 to r11 and d8 to d15 to markers and the rounding mode to plus, and a
// naked caller calls the syscall and reads back what the thread holds. The masks count bits from
// the lowest register, so bit 0 is r4 or d8.
//
// The calls that wait run on a waiter thread pinned to core 1, with a changer thread on the same
// core. The changer clobbers r4 to r11, d0 to d31 and the rounding mode, then signals, cancels or
// deletes the semaphore the waiter waits on, or does nothing. A changer started by the waiter runs
// while the waiter is blocked, and one started by the test runs once the waiter is in its wait.
// The cases are a poll that succeeds, a delay with a changer on the core, a wait woken by a signal,
// a wait that times out after 1 ms, a wait ended by a cancel, a wait ended by a delete, and a poll
// on an unknown UID, which goes last. Each case creates its semaphore and threads fresh.
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

// What the changer does: it sets its own rounding mode and then one of these, or nothing
#define CHANGER_NONE 0
#define CHANGER_SIGNAL 1
#define CHANGER_CANCEL 2
#define CHANGER_DELETE 3

static SceUID sema, changer_thread;
static int changer_mode, changer_first;
static SceUInt wait_timeout;
static Call call;
static volatile unsigned int waiter_rmode;

// Runs on the changer's thread after its asm has clobbered the registers. It sets the rounding
// mode, then does what changer_mode asks.
void changer_act(void);
void changer_act(void) {
    int waiting = 0;
    set_rmode(RMODE_MINUS);
    if (changer_mode == CHANGER_SIGNAL)
        sceKernelSignalSema(sema, 1);
    else if (changer_mode == CHANGER_CANCEL)
        sceKernelCancelSema(sema, 0, &waiting);
    else if (changer_mode == CHANGER_DELETE)
        sceKernelDeleteSema(sema);
}

// Pushes r4 to r11 and d8 to d15, writes 0xCCCCCCCC to r4 to r12 and d0 to d31, calls changer_act,
// then pops the saved registers and returns 0.
#define CHANGER_D(n) "vmov d" #n ", r0, r0\n\t"
__attribute__((naked)) static int changer(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "push {r4-r11, lr}\n\t"
        "vpush {d8-d15}\n\t"
        "push {r0}\n\t"
        "movw r0, #0xCCCC\n\t"
        "movt r0, #0xCCCC\n\t"
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
        CHANGER_D(0) CHANGER_D(1) CHANGER_D(2) CHANGER_D(3) CHANGER_D(4) CHANGER_D(5) CHANGER_D(6)
        CHANGER_D(7) CHANGER_D(8) CHANGER_D(9) CHANGER_D(10) CHANGER_D(11) CHANGER_D(12)
        CHANGER_D(13) CHANGER_D(14) CHANGER_D(15) CHANGER_D(16) CHANGER_D(17) CHANGER_D(18)
        CHANGER_D(19) CHANGER_D(20) CHANGER_D(21) CHANGER_D(22) CHANGER_D(23) CHANGER_D(24)
        CHANGER_D(25) CHANGER_D(26) CHANGER_D(27) CHANGER_D(28) CHANGER_D(29) CHANGER_D(30)
        CHANGER_D(31)
        "bl changer_act\n\t"
        "pop {r0}\n\t"
        "vpop {d8-d15}\n\t"
        "movs r0, #0\n\t"
        "pop {r4-r11, pc}\n\t");
}
#undef CHANGER_D

// Runs the call on the waiter's thread, after setting its rounding mode to plus
static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    set_rmode(RMODE_PLUS);
    if (changer_first)
        sceKernelStartThread(changer_thread, 0, NULL);
    call_marked(&call);
    waiter_rmode = get_rmode();
    return 0;
}

#define POLL ((int (*)(int, int, void *))(void *)sceKernelPollSema)
#define WAIT ((int (*)(int, int, void *))(void *)sceKernelWaitSema)
#define DELAY ((int (*)(int, int, void *))(void *)sceKernelDelayThread)

static void set_call(Call *c, int (*fn)(int, int, void *), int a0, int a1, void *a2) {
    c->fn = fn;
    c->a0 = a0;
    c->a1 = a1;
    c->a2 = a2;
    for (int i = 0; i < 8; i++)
        c->d_in[i] = d_markers[i];
}

// Prints what the thread holds after the call, from the registers the call left in c
static void report(const char *name, const Call *c, int rmode) {
    unsigned int r_kept = 0, d_kept = 0;
    for (int i = 0; i < 8; i++) {
        if (c->r_out[i] == r_markers[i])
            r_kept |= 1u << i;
        if (c->d_out[i] == d_markers[i])
            d_kept |= 1u << i;
    }
    PRINTF(c->result, "%s: the call's result", name);
    PRINTF(r_kept, "%s: mask of r4 to r11 that kept their markers", name);
    PRINTF(d_kept, "%s: mask of d8 to d15 that kept their markers", name);
    PRINTF(c->sp_before == c->sp_after, "%s: sp is the same", name);
    PRINTF(rmode, "%s: the rounding mode after", name);
}

// Runs a delay of timeout, or a semaphore wait with timeout, on a waiter thread that shares core 1
// with a changer in mode. An early changer is started by the waiter before the call. A late one is
// started by the test once the waiter is in its semaphore wait.
static void run_waiter_case(const char *name, int mode, int early, int delay, SceUInt timeout) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    wait_timeout = timeout;
    if (delay)
        set_call(&call, DELAY, (int)timeout, 0, NULL);
    else
        set_call(&call, WAIT, sema, 1, &wait_timeout);
    changer_mode = mode;
    changer_first = early;

    const SceUID waiter_thread = sceKernelCreateThread("waiter", waiter, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    changer_thread = sceKernelCreateThread("changer", changer, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(waiter_thread, 0, NULL);
    if (!early) {
        TEST_WAIT_FOR(test_in_wait(waiter_thread, WAIT_TYPE_SEMA, sema));
        sceKernelStartThread(changer_thread, 0, NULL);
    }
    test_join_thread(changer_thread);
    test_join_thread(waiter_thread);
    if (mode != CHANGER_DELETE)
        sceKernelDeleteSema(sema);
    report(name, &call, waiter_rmode);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    set_call(&call, POLL, sema, 1, NULL);
    set_rmode(RMODE_PLUS);
    call_marked(&call);
    report("PollSema with a count", &call, get_rmode());
    sceKernelDeleteSema(sema);

    run_waiter_case("DelayThread(1000) with a changer", CHANGER_NONE, 1, 1, 1000);
    run_waiter_case("WaitSema woken by a signal", CHANGER_SIGNAL, 0, 0, TEST_WAIT_US);
    run_waiter_case("WaitSema timed out after 1 ms", CHANGER_NONE, 1, 0, 1000);
    run_waiter_case("WaitSema ended by a cancel", CHANGER_CANCEL, 0, 0, TEST_WAIT_US);
    run_waiter_case("WaitSema ended by a delete", CHANGER_DELETE, 0, 0, TEST_WAIT_US);

    set_call(&call, POLL, 0x12345, 1, NULL);
    set_rmode(RMODE_PLUS);
    call_marked(&call);
    report("PollSema with an unknown UID", &call, get_rmode());

    test_exit();
    return 0;
}
