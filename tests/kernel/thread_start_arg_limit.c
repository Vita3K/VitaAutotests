// The largest start argument a thread takes. StartThread copies an argument to the top of the
// thread's stack, rounded up to 16 bytes, and refuses one bigger than the stack. The cases use a
// 0x10000 stack with sizes 0xFF00, 0xFFF0 and 0x10001, and a thread created with a 0x100 stack,
// which info reports as 0x1000, with size 0x1001. A size that fills the whole stack once rounded is
// left out, since such a start crashed the app on a PS Vita TV.
// Each case prints the start result, the stack size info reports, whether the thread ran, and where
// its registers point: r0 is the size, r1 is the copy and sp is where the copy starts, shown as the
// distance below the stack top. It also prints whether r1 equals sp, and whether the copy holds the
// bytes sent. The entry is naked and pushes nothing, since a copy leaves little room below it. It
// stores its registers in a global for the main thread to read after
// the thread has ended. Each case starts a new thread.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// What the entry saw in its registers, and whether it ran
typedef struct StartSeen {
    unsigned int r0, r1, sp, ran;
} StartSeen;

// Written by start_entry, read by main once the thread has ended
StartSeen start_seen;

// The argument bytes, sized for the largest case with room behind it
static unsigned char arg_bytes[0x10100];

// The bounds of the stack of the thread the current case started
static unsigned int stack_low, stack_high;

// Stores r0, r1 and sp, which hold the size, the copy and the stack pointer at entry, then returns
// 0. It uses no stack and makes no call.
__attribute__((naked)) static int start_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    __asm__ volatile(
        "movw r2, #:lower16:start_seen\n\t"
        "movt r2, #:upper16:start_seen\n\t"
        "str r0, [r2, #0]\n\t"
        "str r1, [r2, #4]\n\t"
        "str sp, [r2, #8]\n\t"
        "movs r3, #1\n\t"
        "str r3, [r2, #12]\n\t"
        "movs r0, #0\n\t"
        "bx lr\n\t");
}

// Prints where value is relative to the stack top, or that it is outside the stack
static void print_position(const char *name, const char *reg, unsigned int value) {
    const int inside = value >= stack_low && value <= stack_high;
    PRINTF(inside, "%s: %s inside the stack", name, reg);
    if (inside)
        PRINTF(stack_high - value, "%s: %s, below the stack top by", name, reg);
}

// Returns whether the size bytes at r1 are the argument sent, and lie inside the stack
static int copy_matches(unsigned int r1, unsigned int size) {
    return r1 >= stack_low && r1 <= stack_high && size <= stack_high - r1 &&
           sceClibMemcmp((const void *)r1, arg_bytes, size) == 0;
}

// Starts a thread with a stack of stack_size on an argument of size bytes, waits for it to end and
// prints what its registers held. A start that fails leaves the thread dormant, so it isn't waited
// for.
static void run_case(const char *name, SceSize stack_size, SceSize size) {
    start_seen = (StartSeen){ 0 };
    const SceUID thread = sceKernelCreateThread("start args", start_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, stack_size, 0, 0, NULL);
    const int started = sceKernelStartThread(thread, size, arg_bytes);
    PRINTF(started, "%s: start", name);
    if (started == 0) {
        SceUInt timeout = TEST_WAIT_US;
        sceKernelWaitThreadEnd(thread, NULL, &timeout);
    }

    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    stack_low = (unsigned int)info.stack;
    stack_high = stack_low + info.stackSize;
    PRINTF(info.stackSize, "%s: stack size", name);
    PRINTF(start_seen.ran, "%s: the thread ran", name);
    if (start_seen.ran) {
        PRINTF(start_seen.r0 == size, "%s: r0 is the size", name);
        print_position(name, "r1", start_seen.r1);
        print_position(name, "sp", start_seen.sp);
        PRINTF(start_seen.r1 == start_seen.sp, "%s: r1 == sp", name);
        PRINTF(copy_matches(start_seen.r1, size), "%s: the copy holds the bytes sent", name);
    }
    sceKernelDeleteThread(thread);
}

int main(void) {
    for (unsigned int i = 0; i < sizeof(arg_bytes); i++)
        arg_bytes[i] = (unsigned char)(i + (i >> 8));

    run_case("stack 0x10000, size 0xFF00", 0x10000, 0xFF00);
    run_case("stack 0x10000, size 0xFFF0", 0x10000, 0xFFF0);
    run_case("stack 0x10000, size 0x10001", 0x10000, 0x10001);
    run_case("stack 0x100, size 0x1001", 0x100, 0x1001);

    test_exit();
    return 0;
}
