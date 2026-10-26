// sceKernelCallWithChangeStack, which calls a function on a buffer the caller gives as its
// stack, and gives the thread its own stack bounds back after the call. The function's sp is
// inside the buffer, TLS slots 2 and 3 hold the buffer's top and bottom, and
// sceKernelCheckThreadStack counts from the buffer's bottom. The thread's info stack, TPIDRURO,
// thread ID and FPSCR stay as they were, and so do r4 to r11 and sp across the call. The test
// prints nothing from the new stack. The function records what it sees, and the printing happens
// after the call returns. Cases:
// - a call with a 0x2000 buffer, aligned to 4 KiB, whose function returns its argument plus 1
// - the same call, made by a naked block that holds markers in r4 to r11, checked after the return
// - a nested call from inside the function, with a second buffer: the inner call gives back the
//   outer buffer's slots, and the outer call gives back the thread's own
// The function is a naked entry that pushes its registers and calls a C helper, which records
// them. The helper runs on the new stack too. Bad pointers and callbacks inside the call aren't
// tested. The import isn't declared in the headers the tests use, so it's declared here.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

// The function a call runs, which the import calls as func(arg)
typedef int (*ChangeStackFunc)(int arg);

// Switches to the stack from base to base + size, calls func(arg) on it, switches back and returns
// what func returns
int sceKernelCallWithChangeStack(void *base, SceSize size, ChangeStackFunc func, int arg);

#define STACK_SIZE 0x2000
// The outer call and the nested call of a case each get a record
#define RECORDS 2
#define OUTER_ARG 0x40
#define NESTED_ARG 0x70

// What the function saw, from its naked entry and in the helper it calls
typedef struct Seen {
    int ran;
    unsigned int arg, r1, r2, r3;
    unsigned int sp;
    unsigned int here;
    unsigned int slot2, slot3;
    int check;
    unsigned int info_stack, info_stack_size;
    unsigned int tpidruro, thread_id, fpscr;
    unsigned int slot2_after_nested, slot3_after_nested;
} Seen;

// The thread's own state, read before the first call
typedef struct Own {
    unsigned int stack, stack_size;
    unsigned int tpidruro, thread_id, fpscr;
    unsigned int slot2, slot3;
} Own;

static Seen seen[RECORDS];
static int next_record;
// Set by a case: the outer call makes a nested call with stack_b
static int nest;
static int nested_ret;
static Own own;

static unsigned char stack_a[STACK_SIZE] __attribute__((aligned(0x1000)));
static unsigned char stack_b[STACK_SIZE] __attribute__((aligned(0x1000)));

static unsigned int tls_slot(int key) {
    return *(volatile unsigned int *)sceKernelGetTLSAddr(key);
}

static unsigned int get_sp(void) {
    unsigned int value;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static int change_stack_func(int arg);

// Records what the function sees. pushed holds r0 to r3, r12 and lr, as the naked entry
// pushed them.
void record_change_stack(const unsigned int *pushed);
void record_change_stack(const unsigned int *pushed) {
    Seen *s = &seen[next_record++];
    s->ran = 1;
    s->arg = pushed[0];
    s->r1 = pushed[1];
    s->r2 = pushed[2];
    s->r3 = pushed[3];
    // Six words were pushed, so sp was 24 bytes above them
    s->sp = (unsigned int)pushed + 24;
    s->here = get_sp();
    s->slot2 = tls_slot(2);
    s->slot3 = tls_slot(3);
    s->check = sceKernelCheckThreadStack();
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    s->info_stack = (unsigned int)info.stack;
    s->info_stack_size = info.stackSize;
    s->tpidruro = get_tpidruro();
    s->thread_id = sceKernelGetThreadId();
    s->fpscr = get_fpscr();
    if (nest) {
        nest = 0;
        nested_ret =
            sceKernelCallWithChangeStack(stack_b, sizeof(stack_b), change_stack_func, NESTED_ARG);
        s->slot2_after_nested = tls_slot(2);
        s->slot3_after_nested = tls_slot(3);
    }
}

// The function the calls run. Its naked entry pushes its registers, records them, and returns its
// argument plus 1.
__attribute__((naked)) static int change_stack_func(int arg) {
    (void)arg;
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n\t"
        "mov r0, sp\n\t"
        "bl record_change_stack\n\t"
        "pop {r0-r3, r12, lr}\n\t"
        "adds r0, r0, #1\n\t"
        "bx lr\n\t");
}

// What call_marked_change calls with, and what the thread holds after the call. The offsets are
// the ones the asm block uses.
typedef struct ChangeCall {
    void *base;
    SceSize size;
    ChangeStackFunc func;
    int arg;
    void *import;
    int ret;
    unsigned int kept[8];
    unsigned int sp_before;
    unsigned int sp_after;
} ChangeCall;

// Calls the import with markers 0x44444444 to 0xBBBBBBBB in r4 to r11, then stores its
// return value, r4 to r11 and sp into c. The pushes keep r4 to r11 for its caller.
__attribute__((naked)) static void call_marked_change(ChangeCall *c) {
    (void)c;
    __asm__ volatile(
        "push {r0, r4-r11, lr}\n\t"
        "mov r1, sp\n\t"
        "str r1, [r0, #56]\n\t"
        "ldr lr, [r0, #16]\n\t"
        "ldr r1, [r0, #4]\n\t"
        "ldr r2, [r0, #8]\n\t"
        "ldr r3, [r0, #12]\n\t"
        "ldr r0, [r0, #0]\n\t"
        "movw r4, #0x4444\n\tmovt r4, #0x4444\n\t"
        "movw r5, #0x5555\n\tmovt r5, #0x5555\n\t"
        "movw r6, #0x6666\n\tmovt r6, #0x6666\n\t"
        "movw r7, #0x7777\n\tmovt r7, #0x7777\n\t"
        "movw r8, #0x8888\n\tmovt r8, #0x8888\n\t"
        "movw r9, #0x9999\n\tmovt r9, #0x9999\n\t"
        "movw r10, #0xAAAA\n\tmovt r10, #0xAAAA\n\t"
        "movw r11, #0xBBBB\n\tmovt r11, #0xBBBB\n\t"
        "blx lr\n\t"
        "mov r1, sp\n\t"
        "ldr r12, [sp, #0]\n\t"
        "str r1, [r12, #60]\n\t"
        "str r0, [r12, #20]\n\t"
        "str r4, [r12, #24]\n\t"
        "str r5, [r12, #28]\n\t"
        "str r6, [r12, #32]\n\t"
        "str r7, [r12, #36]\n\t"
        "str r8, [r12, #40]\n\t"
        "str r9, [r12, #44]\n\t"
        "str r10, [r12, #48]\n\t"
        "str r11, [r12, #52]\n\t"
        "pop {r0, r4-r11, pc}\n\t");
}

// Returns a mask with bit i set when r(4 + i) holds its marker after the call
static unsigned int kept_mask(const ChangeCall *c) {
    unsigned int mask = 0;
    for (int i = 0; i < 8; i++) {
        if (c->kept[i] == 0x44444444u + i * 0x11111111u)
            mask |= 1u << i;
    }
    return mask;
}

// Returns the bucket of the distance from sp below top: 0 under 256 bytes, 1 under 1 KiB, 2 under
// 4 KiB, 3 for more
static int distance_bucket(SceUIntPtr top, unsigned int sp) {
    const unsigned int distance = top - sp;
    if (distance < 0x100)
        return 0;
    if (distance < 0x400)
        return 1;
    if (distance < 0x1000)
        return 2;
    return 3;
}

// Prints what a record of the function saw, for a call with a buffer from base to base + size and
// the argument arg
static void report(const char *name, const Seen *s, SceUIntPtr base, SceSize size, int arg) {
    const SceUIntPtr top = base + size;
    PRINTF(s->ran, "%s: the function ran", name);
    PRINTF(s->arg == (unsigned int)arg, "%s: r0 is the argument", name);
    PRINTF(s->sp & 7, "%s: sp & 7 on entry", name);
    PRINTF(s->sp & 15, "%s: sp & 15 on entry", name);
    // The top counts as inside, as the kernel's own check has it, since sp starts at the top
    PRINTF(s->sp >= base && s->sp <= top, "%s: sp is inside the buffer", name);
    PRINTF(distance_bucket(top, s->sp), "%s: sp's distance below the top, bucket", name);
    PRINTF(s->slot2 == top, "%s: slot 2 is the top of the buffer", name);
    PRINTF(s->slot3 == base, "%s: slot 3 is the bottom of the buffer", name);
    PRINTF(s->check == (int)(s->here - base), "%s: CheckThreadStack is sp minus the bottom", name);
    PRINTF(s->info_stack == own.stack && s->info_stack_size == own.stack_size,
           "%s: the thread info stack and size are unchanged", name);
    PRINTF(s->tpidruro == own.tpidruro, "%s: TPIDRURO is unchanged", name);
    PRINTF(s->thread_id == own.thread_id, "%s: the thread ID is unchanged", name);
    PRINTF(s->fpscr == own.fpscr, "%s: FPSCR is unchanged", name);
}

int main(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(0, &info);
    own.stack = (unsigned int)info.stack;
    own.stack_size = info.stackSize;
    own.tpidruro = get_tpidruro();
    own.thread_id = sceKernelGetThreadId();
    own.fpscr = get_fpscr();
    own.slot2 = tls_slot(2);
    own.slot3 = tls_slot(3);

    // One call, with a 0x2000 buffer and the argument 0x29
    for (int i = 0; i < RECORDS; i++)
        seen[i] = (Seen){ 0 };
    next_record = 0;
    nest = 0;
    ChangeCall call = {
        .base = stack_a, .size = sizeof(stack_a), .func = change_stack_func, .arg = 0x29,
        .import = (void *)sceKernelCallWithChangeStack,
    };
    call_marked_change(&call);
    PRINT("the call returns the function's value", call.ret);
    report("call", &seen[0], (SceUIntPtr)stack_a, sizeof(stack_a), 0x29);
    PRINT("call: slot 2 is the thread's own again", tls_slot(2) == own.slot2);
    PRINT("call: slot 3 is the thread's own again", tls_slot(3) == own.slot3);
    PRINT("call: sp is the same after the call", call.sp_after == call.sp_before);
    PRINT("call: r4 to r11 kept, bit 0 is r4", kept_mask(&call));

    // A nested call: the function makes a second call with stack_b, from its own stack
    for (int i = 0; i < RECORDS; i++)
        seen[i] = (Seen){ 0 };
    next_record = 0;
    nest = 1;
    nested_ret = 0;
    ChangeCall outer = {
        .base = stack_a, .size = sizeof(stack_a), .func = change_stack_func, .arg = OUTER_ARG,
        .import = (void *)sceKernelCallWithChangeStack,
    };
    call_marked_change(&outer);
    PRINT("nested: the outer call returns the function's value", outer.ret);
    PRINT("nested: the inner call returns the function's value", nested_ret);
    report("nested outer", &seen[0], (SceUIntPtr)stack_a, sizeof(stack_a), OUTER_ARG);
    report("nested inner", &seen[1], (SceUIntPtr)stack_b, sizeof(stack_b), NESTED_ARG);
    PRINT("nested: after the inner call, slot 2 is the outer buffer's top",
          seen[0].slot2_after_nested == (SceUIntPtr)stack_a + sizeof(stack_a));
    PRINT("nested: after the inner call, slot 3 is the outer buffer's bottom",
          seen[0].slot3_after_nested == (SceUIntPtr)stack_a);
    PRINT("nested: after the outer call, slot 2 is the thread's own", tls_slot(2) == own.slot2);
    PRINT("nested: after the outer call, slot 3 is the thread's own", tls_slot(3) == own.slot3);
    PRINT("nested: sp is the same after the call", outer.sp_after == outer.sp_before);
    PRINT("nested: r4 to r11 kept, bit 0 is r4", kept_mask(&outer));

    test_exit();
    return 0;
}
