// What a callback sees when it starts: whether its sp is 8 byte aligned and where it lies relative
// to the caller's sp, whether each of r4 to r11 holds the thread's own marker,
// lr's Thumb bit and whether lr is the same as in the first case, and the full FPSCR. The callbacks
// are naked and record their entry state before anything changes a register, then return 0. Thumb
// and ARM callbacks are both used. The callback arguments are only checked in the ARM case.
//
// Each case starts a fresh thread that creates its callback, notifies it so it is pending, and then
// calls one of sceKernelCheckCallback, sceKernelWaitSemaCB with a 1 ms timeout, or
// sceKernelDelayThreadCB for 1 ms. The call is made from naked code that sets r4 to r11 to markers,
// so the test shows whether the callback sees the thread's own values. That code either calls with
// sp 8 byte aligned or moves sp down 4 bytes around the call. A nested case has a Thumb callback
// that notifies a second callback and checks for it from inside itself, and the second callback
// records its own entry state. Cases: a Thumb and an ARM callback, each run by a check, a CB wait
// and a CB delay, aligned and 4 bytes off, then a nested callback.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MARKER_BASE 0x44444444u
#define MARKER_STEP 0x11111111u
#define NOTIFY_ARG 0x1234

enum { CALL_CHECK, CALL_WAIT, CALL_DELAY };

// What a callback saw when it started
typedef struct Seen {
    int ran, args_ok;
    unsigned int r[8], sp, lr, fpscr;
} Seen;

static Seen seen, inner_seen;
static int common_marker;
static volatile unsigned int caller_sp, outer_sp, first_lr;
static volatile SceUID cb_uid, inner_uid, sema;
static volatile int call_result, inner_result;
static volatile int case_kind, case_off, case_arm, case_nested;
static int first_case = 1;

// Stores what a callback saw. pushed holds r0 to r12 and lr, as the naked entries pushed them.
static void store(Seen *s, const unsigned int *pushed, unsigned int fpscr) {
    s->ran++;
    for (int i = 0; i < 8; i++)
        s->r[i] = pushed[i + 4];
    s->sp = (unsigned int)pushed + 14 * 4;
    s->lr = pushed[13];
    s->fpscr = fpscr;
    s->args_ok = pushed[0] == (unsigned int)-1 && pushed[1] == 1 && pushed[2] == NOTIFY_ARG &&
                 pushed[3] == (unsigned int)(SceUIntPtr)&common_marker;
}

// Called by the naked callbacks with the registers they pushed and the FPSCR they read first
void record_callback(const unsigned int *pushed, unsigned int fpscr);
void record_callback(const unsigned int *pushed, unsigned int fpscr) {
    store(&seen, pushed, fpscr);
}

// Called by the naked inner callback, the one a nested callback notifies
void record_inner(const unsigned int *pushed, unsigned int fpscr);
void record_inner(const unsigned int *pushed, unsigned int fpscr) {
    store(&inner_seen, pushed, fpscr);
}

// Run by the nested callback: records its sp, notifies the inner callback and checks for it
void outer_body(void);
void outer_body(void) {
    unsigned int sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    outer_sp = sp;
    sceKernelNotifyCallback(inner_uid, NOTIFY_ARG);
    inner_result = sceKernelCheckCallback();
}

// Pushes every register and records the entry state before anything changes one, then returns 0
__attribute__((naked)) static int thumb_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "vmrs r1, fpscr\n\t"
        "mov r0, sp\n\t"
        "bl record_callback\n\t"
        "pop {r0-r12, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// The same as thumb_callback, compiled as ARM code
__attribute__((naked, target("arm"))) static int arm_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "vmrs r1, fpscr\n\t"
        "mov r0, sp\n\t"
        "bl record_callback\n\t"
        "pop {r0-r12, lr}\n\t"
        "mov r0, #0\n\t"
        "bx lr\n\t");
}

// A Thumb callback that records like thumb_callback, then runs outer_body, which nests a callback
__attribute__((naked)) static int outer_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "vmrs r1, fpscr\n\t"
        "mov r0, sp\n\t"
        "bl record_callback\n\t"
        "bl outer_body\n\t"
        "pop {r0-r12, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// The callback that outer_callback nests, which records like thumb_callback into inner_seen
__attribute__((naked)) static int inner_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "vmrs r1, fpscr\n\t"
        "mov r0, sp\n\t"
        "bl record_inner\n\t"
        "pop {r0-r12, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// Sets r4 to r11 to their markers, for the calls below
#define SET_MARKERS                                                                               \
    "movw r4, #0x4444\n\tmovt r4, #0x4444\n\t"                                                    \
    "movw r5, #0x5555\n\tmovt r5, #0x5555\n\t"                                                    \
    "movw r6, #0x6666\n\tmovt r6, #0x6666\n\t"                                                    \
    "movw r7, #0x7777\n\tmovt r7, #0x7777\n\t"                                                    \
    "movw r8, #0x8888\n\tmovt r8, #0x8888\n\t"                                                    \
    "movw r9, #0x9999\n\tmovt r9, #0x9999\n\t"                                                    \
    "movw r10, #0xAAAA\n\tmovt r10, #0xAAAA\n\t"                                                  \
    "movw r11, #0xBBBB\n\tmovt r11, #0xBBBB\n\t"

// Calls fn(a0, a1, a2) with r4 to r11 set to markers, with sp 8 byte aligned at the call, and
// stores that sp in caller_sp
__attribute__((naked)) static int call_aligned(int a0, int a1, int a2, int (*fn)(int, int, int)) {
    (void)a0;
    (void)a1;
    (void)a2;
    (void)fn;
    __asm__ volatile(
        "push {r3-r11, lr}\n\t"
        SET_MARKERS
        "mov lr, sp\n\t"
        "movw r12, :lower16:caller_sp\n\t"
        "movt r12, :upper16:caller_sp\n\t"
        "str lr, [r12]\n\t"
        "blx r3\n\t"
        "pop {r3-r11, pc}\n\t");
}

// The same as call_aligned, with sp moved down 4 bytes for the call and back after it
__attribute__((naked)) static int call_off4(int a0, int a1, int a2, int (*fn)(int, int, int)) {
    (void)a0;
    (void)a1;
    (void)a2;
    (void)fn;
    __asm__ volatile(
        "push {r3-r11, lr}\n\t"
        SET_MARKERS
        "sub sp, sp, #4\n\t"
        "mov lr, sp\n\t"
        "movw r12, :lower16:caller_sp\n\t"
        "movt r12, :upper16:caller_sp\n\t"
        "str lr, [r12]\n\t"
        "blx r3\n\t"
        "add sp, sp, #4\n\t"
        "pop {r3-r11, pc}\n\t");
}

// The entry of a case: creates the callbacks, notifies the first one, makes the call and deletes
static int case_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceKernelCallbackFunction fn = thumb_callback;
    if (case_arm)
        fn = arm_callback;
    else if (case_nested)
        fn = outer_callback;
    cb_uid = sceKernelCreateCallback("entry", 0, fn, &common_marker);
    if (case_nested)
        inner_uid = sceKernelCreateCallback("inner", 0, inner_callback, &common_marker);
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    sceKernelNotifyCallback(cb_uid, NOTIFY_ARG);

    SceUInt timeout = 1000;
    int (*call)(int, int, int) = (int (*)(int, int, int))(void *)sceKernelCheckCallback;
    int a0 = 0, a1 = 0, a2 = 0;
    if (case_kind == CALL_WAIT) {
        call = (int (*)(int, int, int))(void *)sceKernelWaitSemaCB;
        a0 = sema;
        a1 = 1;
        a2 = (int)(SceUIntPtr)&timeout;
    } else if (case_kind == CALL_DELAY) {
        call = (int (*)(int, int, int))(void *)sceKernelDelayThreadCB;
        a0 = 1000;
    }
    call_result = case_off ? call_off4(a0, a1, a2, call) : call_aligned(a0, a1, a2, call);

    sceKernelDeleteCallback(cb_uid);
    if (case_nested)
        sceKernelDeleteCallback(inner_uid);
    sceKernelDeleteSema(sema);
    return 0;
}

// Whether a register's value is the thread's marker for register index 0 to 7 (r4 to r11)
static int holds_marker(unsigned int value, int index) {
    return value == MARKER_BASE + MARKER_STEP * (unsigned int)index;
}

// Runs one case on a fresh thread and prints what the callback saw
static void run_case(const char *name, int kind_of_call, int off, int arm, int nested) {
    case_kind = kind_of_call;
    case_off = off;
    case_arm = arm;
    case_nested = nested;
    seen = (Seen){ 0 };
    inner_seen = (Seen){ 0 };
    inner_result = -1;
    caller_sp = outer_sp = 0;
    const SceUID thread = sceKernelCreateThread("entry state", case_thread, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
    sceKernelDeleteThread(thread);

    if (first_case) {
        first_lr = seen.lr;
        first_case = 0;
    }
    PRINTF(call_result, "%s: the call returns", name);
    PRINTF(seen.ran, "%s: the callback ran", name);
    PRINTF(caller_sp & 7, "%s: the caller's sp & 7", name);
    PRINTF(seen.sp & 7, "%s: the callback's sp & 7", name);
    PRINTF(seen.sp & 15, "%s: the callback's sp & 15", name);
    PRINTF(seen.sp == caller_sp, "%s: the callback's sp is the caller's sp", name);
    PRINTF(seen.sp <= caller_sp && caller_sp - seen.sp < 0x400, "%s: the callback's sp is less than 1 KiB below the caller's", name);
    for (int i = 0; i < 8; i++)
        PRINTF(holds_marker(seen.r[i], i), "%s: r%d holds the thread's marker", name, 4 + i);
    PRINTF(seen.lr & 1, "%s: lr is Thumb", name);
    PRINTF(seen.lr == first_lr, "%s: lr is the same as in the first case", name);
    PRINTF(seen.fpscr, "%s: FPSCR at entry", name);
    if (arm)
        PRINTF(seen.args_ok, "%s: r0 to r3 are the notify ID, count, argument and common pointer", name);
    if (nested) {
        PRINTF(inner_seen.ran, "%s: the nested callback ran", name);
        PRINTF(inner_seen.sp & 7, "%s: the nested callback's sp & 7", name);
        PRINTF(inner_seen.sp & 15, "%s: the nested callback's sp & 15", name);
        PRINTF(outer_sp < seen.sp && seen.sp - outer_sp < 0x400, "%s: the outer callback's body is less than 1 KiB below its entry", name);
        PRINTF(inner_seen.sp == seen.sp, "%s: the nested callback's sp is the outer callback's entry sp", name);
        PRINTF(inner_seen.sp > outer_sp, "%s: the nested callback's sp is above the outer callback's body", name);
        PRINTF(inner_seen.lr & 1, "%s: the nested callback's lr is Thumb", name);
        PRINTF(inner_result, "%s: the check inside the outer callback returns", name);
    }
}

int main(void) {
    run_case("a Thumb callback run by a check", CALL_CHECK, 0, 0, 0);
    run_case("a Thumb callback run by a check, sp 4 bytes off", CALL_CHECK, 1, 0, 0);
    run_case("a Thumb callback run by a CB wait", CALL_WAIT, 0, 0, 0);
    run_case("a Thumb callback run by a CB wait, sp 4 bytes off", CALL_WAIT, 1, 0, 0);
    run_case("a Thumb callback run by a CB delay", CALL_DELAY, 0, 0, 0);
    run_case("a Thumb callback run by a CB delay, sp 4 bytes off", CALL_DELAY, 1, 0, 0);
    run_case("a nested Thumb callback", CALL_CHECK, 0, 0, 1);
    run_case("an ARM callback run by a check", CALL_CHECK, 0, 1, 0);
    run_case("an ARM callback run by a check, sp 4 bytes off", CALL_CHECK, 1, 1, 0);
    run_case("an ARM callback run by a CB wait", CALL_WAIT, 0, 1, 0);
    run_case("an ARM callback run by a CB wait, sp 4 bytes off", CALL_WAIT, 1, 1, 0);
    run_case("an ARM callback run by a CB delay", CALL_DELAY, 0, 1, 0);
    run_case("an ARM callback run by a CB delay, sp 4 bytes off", CALL_DELAY, 1, 1, 0);

    test_exit();
    return 0;
}
