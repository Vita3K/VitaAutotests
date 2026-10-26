// The stack, registers and TPIDRURO that a thread event handler runs with, and the registers its
// thread's entry starts with after the handlers ran. The entry takes a 40 byte argument. It records
// r0 and r1, r2 to r12 and its sp, and whether the copy holds the bytes sent. The run with no
// handler is the reference: the entry's r2 to r12 are printed as the mask of registers equal to it,
// and its sp as the same distance below the stack top.
//
// Each handler records its type, the thread, the argument, the common pointer, whether it runs on
// the target, whether its sp is on the target's stack and less than 4 KiB below the top, sp & 7 and
// sp & 15, and whether TPIDRURO is the thread's TLS address for key 0 plus 0x800. A START handler's
// sp is compared with the argument copy, and an END handler's sp with the entry's sp, since the
// entry has returned by then. The C START handler fills a 512 byte local buffer, which must lie on
// the stack below the copy. Every value printed is a comparison, a mask or a small value, since
// stack addresses differ between runs.
//
// The cases, each on a fresh thread unless said otherwise: no handler, a START handler in C, a
// START and an END handler in C, the same thread started again with those two still registered, a
// START handler compiled as ARM code, and last a START and an END handler in naked assembly that
// clobber r0 to r12 and d0 to d31 after they record. The ARM and the clobbering handlers come after
// the C cases, and the clobbering ones last, since a wrong implementation may crash on them.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

#define ARG_SIZE 40

// The argument sent to the entry, each byte different from the next
static unsigned char known[ARG_SIZE] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
};

// What the entry started with
typedef struct Entry {
    int ran, copy_ok;
    unsigned int r0, r1, r[11], sp, stack_low, stack_high;
} Entry;

// What a handler was called with, and where it ran
typedef struct HookCall {
    unsigned int sp, buf_lo, buf_hi;
    SceInt32 type, arg;
    SceUID thread_id;
    void *common;
} HookCall;

// What a handler saw, over all its runs in a case
typedef struct Hook {
    int runs, type, arg, thread_ok, on_target, common_ok, on_stack, near_top, tls_ok, buf_on_stack;
    unsigned int sp, buf_lo, buf_hi;
} Hook;

#define HOOK_START 0
#define HOOK_END 1

static Entry entry_seen;
static Hook hooks[2];
static int common_marker;
static volatile SceUID target;

// Called by the entry with the registers it pushed
void record_entry(const unsigned int *pushed);
void record_entry(const unsigned int *pushed) {
    Entry *e = &entry_seen;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(sceKernelGetThreadId(), &info);
    e->stack_low = (unsigned int)info.stack;
    e->stack_high = e->stack_low + info.stackSize;
    e->ran = 1;
    e->r0 = pushed[0];
    e->r1 = pushed[1];
    for (int i = 0; i < 11; i++)
        e->r[i] = pushed[i + 2];
    e->sp = (unsigned int)pushed + 14 * 4;
    // The size is only used once it is known to be small and the copy to be inside the stack
    e->copy_ok = e->r0 <= sizeof(known) && e->r1 >= e->stack_low && e->r1 + e->r0 <= e->stack_high &&
                 sceClibMemcmp((const void *)e->r1, known, e->r0) == 0;
}

// Pushes every register before anything changes one, records them, and returns 0
__attribute__((naked)) static int entry_regs(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "push {r0-r12, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_entry\n\t"
        "pop {r0-r12, lr}\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// Records one handler call, reading the thread's stack bounds, TLS and TPIDRURO from inside it
static void record_hook(const HookCall *call) {
    Hook *h = &hooks[call->type == SCE_KERNEL_THREAD_EVENT_TYPE_START ? HOOK_START : HOOK_END];
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(call->thread_id, &info);
    const unsigned int low = (unsigned int)info.stack, high = low + info.stackSize;
    h->runs++;
    h->type = call->type;
    h->arg = call->arg;
    h->thread_ok = call->thread_id == target;
    h->on_target = sceKernelGetThreadId() == target;
    h->common_ok = call->common == &common_marker;
    h->sp = call->sp;
    h->on_stack = call->sp >= low && call->sp < high;
    h->near_top = high - call->sp < 0x1000;
    h->tls_ok = (unsigned int)(SceUIntPtr)sceKernelGetThreadTLSAddr(call->thread_id, 0) + SCE_KERNEL_TLS_SIZE == get_tpidruro();
    h->buf_lo = call->buf_lo;
    h->buf_hi = call->buf_hi;
    h->buf_on_stack = call->buf_hi != 0 && call->buf_lo >= low && call->buf_hi <= high;
}

// A START handler in C that fills a 512 byte local buffer. The stores are volatile, so the buffer
// stays in the frame.
static int start_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    volatile unsigned char buf[512];
    for (int i = 0; i < 512; i++)
        buf[i] = (unsigned char)(i ^ 0x5C);
    unsigned int sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    const HookCall call = {
        .sp = sp,
        .buf_lo = (unsigned int)(SceUIntPtr)buf,
        .buf_hi = (unsigned int)(SceUIntPtr)buf + sizeof(buf),
        .type = type,
        .arg = arg,
        .thread_id = thread_id,
        .common = common,
    };
    record_hook(&call);
    return 0;
}

// An END handler in C
static int end_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    unsigned int sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    const HookCall call = { .sp = sp, .type = type, .arg = arg, .thread_id = thread_id, .common = common };
    record_hook(&call);
    return 0;
}

// Called by naked_hook with the registers it pushed
void record_naked_hook(const unsigned int *pushed);
void record_naked_hook(const unsigned int *pushed) {
    const HookCall call = {
        .sp = (unsigned int)pushed + 6 * 4,
        .type = pushed[0],
        .arg = pushed[2],
        .thread_id = pushed[1],
        .common = (void *)(SceUIntPtr)pushed[3],
    };
    record_hook(&call);
}

// Moves r1 into both halves of d<n>, for naked_hook
#define VMOV_D(n) "vmov d" #n ", r1, r1\n\t"

// Pushes r0 to r4 and lr, records through record_naked_hook, then clobbers r0 to r12 and d0 to d31
// and returns 0. The six words keep sp 8 byte aligned for the call.
__attribute__((naked)) static int naked_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)type;
    (void)thread_id;
    (void)arg;
    (void)common;
    __asm__ volatile(
        "push {r0-r4, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_naked_hook\n\t"
        "pop {r0-r4, lr}\n\t"
        "movw r0, #0xBAD0\n\tmovt r0, #0xC1A0\n\t"
        "movw r1, #0xBAD1\n\tmovt r1, #0xC1A1\n\t"
        "mov r2, r0\n\tmov r3, r0\n\tmov r4, r0\n\tmov r5, r0\n\t"
        "mov r6, r0\n\tmov r7, r0\n\tmov r8, r0\n\tmov r9, r0\n\t"
        "mov r10, r0\n\tmov r11, r0\n\tmov r12, r0\n\t"
        VMOV_D(0) VMOV_D(1) VMOV_D(2) VMOV_D(3) VMOV_D(4) VMOV_D(5) VMOV_D(6) VMOV_D(7)
        VMOV_D(8) VMOV_D(9) VMOV_D(10) VMOV_D(11) VMOV_D(12) VMOV_D(13) VMOV_D(14) VMOV_D(15)
        VMOV_D(16) VMOV_D(17) VMOV_D(18) VMOV_D(19) VMOV_D(20) VMOV_D(21) VMOV_D(22) VMOV_D(23)
        VMOV_D(24) VMOV_D(25) VMOV_D(26) VMOV_D(27) VMOV_D(28) VMOV_D(29) VMOV_D(30) VMOV_D(31)
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// The START handler compiled as ARM code
__attribute__((target("arm"))) static int arm_hook(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    unsigned int sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    const HookCall call = { .sp = sp, .type = type, .arg = arg, .thread_id = thread_id, .common = common };
    record_hook(&call);
    return 0;
}

static SceUID create_target(void) {
    return sceKernelCreateThread("target", entry_regs, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, 0, NULL);
}

// Registers handler for the events in mask on thread, and prints whether it worked
static SceUID register_hook(const char *label, SceUID thread, SceInt32 mask, SceKernelThreadEventHandler handler) {
    const SceUID id = sceKernelRegisterThreadEventHandler("stack", thread, mask, handler, &common_marker);
    PRINTF(test_created(id), "%s: register", label);
    return id;
}

// Starts thread with the argument, waits for its end and clears what the last run recorded
static void run_target(const char *name, SceUID thread) {
    target = thread;
    entry_seen = (Entry){ 0 };
    hooks[HOOK_START] = (Hook){ 0 };
    hooks[HOOK_END] = (Hook){ 0 };
    PRINTF(sceKernelStartThread(thread, ARG_SIZE, known), "%s: start", name);
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(thread, NULL, &timeout);
}

// Prints the entry of the run with no handler, which the other runs are compared with
static void print_reference(const char *name, const Entry *e) {
    PRINTF(e->ran, "%s: the entry ran", name);
    PRINTF(e->r0, "%s: r0", name);
    PRINTF(e->copy_ok, "%s: the copy holds the bytes sent", name);
    PRINTF(e->stack_high - e->sp, "%s: sp below the stack top by", name);
}

// Prints the entry of a run against the reference
static void print_entry(const char *name, const Entry *ref, const Entry *e) {
    unsigned int equal = 0;
    for (int i = 0; i < 11; i++)
        equal |= (unsigned int)(e->r[i] == ref->r[i]) << i;
    PRINTF(e->ran, "%s: the entry ran", name);
    PRINTF(e->r0, "%s: r0", name);
    PRINTF(e->copy_ok, "%s: the copy holds the bytes sent", name);
    PRINTF(equal, "%s: mask of r2 to r12 equal to the run with no handler", name);
    PRINTF(e->stack_high - e->sp == ref->stack_high - ref->sp, "%s: sp at the same distance below the top as with no handler", name);
}

// Prints what a handler saw in a run. A START handler is compared with the argument copy and an END
// handler with the entry's sp, and only the C START handler has a buffer to compare.
static void print_hook(const char *name, const Hook *h, const Entry *e, int start) {
    PRINTF(h->runs, "%s: runs", name);
    PRINTF(h->type, "%s: the type", name);
    PRINTF(h->thread_ok, "%s: the thread is the target", name);
    PRINTF(h->arg, "%s: the argument", name);
    PRINTF(h->common_ok, "%s: the common pointer is the one given", name);
    PRINTF(h->on_target, "%s: it runs on the target", name);
    PRINTF(h->on_stack, "%s: sp is on the target's stack", name);
    PRINTF(h->near_top, "%s: sp is less than 4 KiB below the stack top", name);
    PRINTF(h->sp & 7, "%s: sp & 7", name);
    PRINTF(h->sp & 15, "%s: sp & 15", name);
    if (start)
        PRINTF(h->sp <= e->r1, "%s: sp is at or below the argument copy", name);
    else
        PRINTF(h->sp <= e->sp, "%s: sp is at or below the entry's frame", name);
    if (h->buf_hi)
        PRINTF(h->buf_on_stack && h->buf_hi <= e->r1, "%s: the 512 byte buffer is on the stack, below the copy", name);
    PRINTF(h->tls_ok, "%s: TPIDRURO is the TLS address for key 0 plus 0x800", name);
}

int main(void) {
    // No handler: the reference the other runs are compared with
    SceUID thread = create_target();
    run_target("no handler", thread);
    const Entry ref = entry_seen;
    print_reference("no handler", &ref);
    sceKernelDeleteThread(thread);

    // One START handler in C, which fills a 512 byte buffer
    thread = create_target();
    const SceUID start = register_hook("a START handler in C", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START, start_hook);
    run_target("a START handler in C", thread);
    sceKernelUnregisterThreadEventHandler(start);
    print_entry("a START handler in C", &ref, &entry_seen);
    print_hook("a START handler in C", &hooks[HOOK_START], &entry_seen, 1);
    sceKernelDeleteThread(thread);

    // A START and an END handler in C, then the same thread started again with both registered
    thread = create_target();
    const SceUID c_start = register_hook("a START and an END handler in C: START", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START, start_hook);
    const SceUID c_end = register_hook("a START and an END handler in C: END", thread, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, end_hook);
    run_target("a START and an END handler in C", thread);
    print_entry("a START and an END handler in C", &ref, &entry_seen);
    print_hook("a START and an END handler in C: START", &hooks[HOOK_START], &entry_seen, 1);
    print_hook("a START and an END handler in C: END", &hooks[HOOK_END], &entry_seen, 0);
    run_target("started again", thread);
    print_entry("started again", &ref, &entry_seen);
    print_hook("started again: START", &hooks[HOOK_START], &entry_seen, 1);
    print_hook("started again: END", &hooks[HOOK_END], &entry_seen, 0);
    sceKernelUnregisterThreadEventHandler(c_start);
    sceKernelUnregisterThreadEventHandler(c_end);
    sceKernelDeleteThread(thread);

    // A START handler compiled as ARM code, after the C cases and before the clobbering handlers
    thread = create_target();
    const SceUID arm = register_hook("an ARM START handler", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START, arm_hook);
    run_target("an ARM START handler", thread);
    print_entry("an ARM START handler", &ref, &entry_seen);
    print_hook("an ARM START handler", &hooks[HOOK_START], &entry_seen, 1);
    sceKernelUnregisterThreadEventHandler(arm);
    sceKernelDeleteThread(thread);

    // A START and an END handler in naked assembly that clobber every register they may change.
    // Last of all, since they may crash a wrong implementation, or the console
    thread = create_target();
    const SceUID n_start = register_hook("naked START and END handlers: START", thread, SCE_KERNEL_THREAD_EVENT_TYPE_START, naked_hook);
    const SceUID n_end = register_hook("naked START and END handlers: END", thread, SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, naked_hook);
    run_target("naked START and END handlers", thread);
    print_entry("naked START and END handlers", &ref, &entry_seen);
    print_hook("naked START and END handlers: START", &hooks[HOOK_START], &entry_seen, 1);
    print_hook("naked START and END handlers: END", &hooks[HOOK_END], &entry_seen, 0);
    sceKernelUnregisterThreadEventHandler(n_start);
    sceKernelUnregisterThreadEventHandler(n_end);
    sceKernelDeleteThread(thread);

    test_exit();
    return 0;
}
