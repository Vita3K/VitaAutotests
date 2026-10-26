// The flags of FPSCR, N, Z, C and V and the cumulative exception flags, which the other FPSCR
// tests mask out. A thread, started anew and then restarted, reads them at its start, at its start
// and end handlers and at callbacks run by a check, a wait and a delay. Then it sets flags of its
// own: a division by zero, a float overflow and a saturating NEON add for the cumulative flags, and
// vcmp for N, Z, C and V, which overwrites the condition flags only. The exception setting is 0, so
// none of them traps. The thread keeps its flags across a delay, a wait during which another thread
// on its core sets other flags, and callbacks that set flags of their own, which the thread has
// again after each callback.
//
// The output is the flags at each step, and the full FPSCR where a thread, handler or callback
// starts. Nothing is compared to a fixed value, since the flags are what the console left.
#include <psp2/kernel/threadmgr.h>

#include "registers.h"
#include "test.h"
#include "threadmgr_extra.h"

// The points where a run reads the flags, in the order a run reaches them
enum { START_HANDLER, ENTRY, CB_CHECK, CB_WAIT, CB_DELAY, END_HANDLER, POINTS };
static const char *const point_names[POINTS] = {
    "the start handler", "the entry", "a callback run by a check", "a callback run by a wait",
    "a callback run by a delay", "the end handler",
};

#define RUNS 2
static const char *const run_names[RUNS] = { "first start", "restart" };

typedef struct Seen {
    unsigned int full, flags;
} Seen;

// The flags the thread sees after each step of a run
typedef struct Steps {
    unsigned int dzc, ofc, qc, unordered, greater, equal, less;
    unsigned int after_delay, after_wait, other;
    unsigned int after_check, after_cb_wait, after_cb_delay;
} Steps;

static Seen seen[RUNS][POINTS];
static Steps steps[RUNS];
static volatile int run, point_now;
static SceUID sema, cb, subject;

// The operands of the flag operations: the pairs for vcmp, in less, equal, greater and unordered
// order, 1.0 and 0.0 for the division, 1e300 for the overflow, and the largest signed word twice
// for the add
static const double compares[4][2] = {
    { 1.0, 2.0 }, { 1.0, 1.0 }, { 2.0, 1.0 }, { __builtin_nan(""), 1.0 },
};
static const double ones[2] = { 1.0, 0.0 };
static const double big[2] = { 1e300, 0.0 };
static const unsigned int limit[2] = { 0x7FFFFFFF, 0x7FFFFFFF };

// Compares the pair at in with vcmp, and returns the flags it leaves
static unsigned int compare_flags(const double *in) {
    unsigned int fpscr;
    __asm__ volatile(
        "vldr d0, [%[in]]\n\t"
        "vldr d1, [%[in], #8]\n\t"
        "vcmp.f64 d0, d1\n\t"
        "vmrs %[out], fpscr\n\t"
        : [out] "=r"(fpscr)
        : [in] "r"(in)
        : "d0", "d1", "cc", "memory");
    return fpscr & FPSCR_FLAGS;
}

// Divides 1.0 by 0.0, which sets the division by zero flag, and returns the flags
static unsigned int divide_flags(void) {
    unsigned int fpscr;
    __asm__ volatile(
        "vldr d0, [%[in]]\n\t"
        "vldr d1, [%[in], #8]\n\t"
        "vdiv.f64 d2, d0, d1\n\t"
        "vmrs %[out], fpscr\n\t"
        : [out] "=r"(fpscr)
        : [in] "r"(ones)
        : "d0", "d1", "d2", "memory");
    return fpscr & FPSCR_FLAGS;
}

// Squares 1e300, which overflows, and returns the flags
static unsigned int overflow_flags(void) {
    unsigned int fpscr;
    __asm__ volatile(
        "vldr d0, [%[in]]\n\t"
        "vmul.f64 d1, d0, d0\n\t"
        "vmrs %[out], fpscr\n\t"
        : [out] "=r"(fpscr)
        : [in] "r"(big)
        : "d0", "d1", "memory");
    return fpscr & FPSCR_FLAGS;
}

// Adds the largest signed word to itself with a saturating NEON add, and returns the flags
static unsigned int saturate_flags(void) {
    unsigned int fpscr;
    __asm__ volatile(
        "vldr d0, [%[in]]\n\t"
        "vqadd.s32 d1, d0, d0\n\t"
        "vmrs %[out], fpscr\n\t"
        : [out] "=r"(fpscr)
        : [in] "r"(limit)
        : "d0", "d1", "memory");
    return fpscr & FPSCR_FLAGS;
}

// Records the FPSCR of the running thread at a point of the current run
static void record(int point) {
    Seen *s = &seen[run][point];
    s->full = get_fpscr();
    s->flags = s->full & FPSCR_FLAGS;
}

// Sets the flags of an equal compare, so the callback has flags of its own
static int flags_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    record(point_now);
    compare_flags(compares[1]);
    return 0;
}

// Records the flags of the start and end of a thread
static int subject_handler(SceInt32 type, SceUID thread_id, SceInt32 arg, void *common) {
    (void)thread_id;
    (void)arg;
    (void)common;
    record(type == SCE_KERNEL_THREAD_EVENT_TYPE_START ? START_HANDLER : END_HANDLER);
    return 0;
}

static int subject_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    Steps *s = &steps[run];
    record(ENTRY);
    cb = sceKernelCreateCallback("flags", 0, flags_callback, NULL);

    // Each step keeps the flags of the steps before it, since vcmp only sets N, Z, C and V
    s->dzc = divide_flags();
    s->ofc = overflow_flags();
    s->qc = saturate_flags();
    s->unordered = compare_flags(compares[3]);
    s->greater = compare_flags(compares[2]);
    s->equal = compare_flags(compares[1]);
    s->less = compare_flags(compares[0]);

    sceKernelDelayThread(1000);
    s->after_delay = get_fpscr() & FPSCR_FLAGS;

    // Another thread on this core runs while this one waits, and sets other flags first
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    s->after_wait = get_fpscr() & FPSCR_FLAGS;

    point_now = CB_CHECK;
    sceKernelNotifyCallback(cb, 1);
    sceKernelCheckCallback();
    s->after_check = get_fpscr() & FPSCR_FLAGS;

    // The callback is pending before the wait, which then times out
    point_now = CB_WAIT;
    sceKernelNotifyCallback(cb, 1);
    timeout = 1000;
    sceKernelWaitSemaCB(sema, 1, &timeout);
    s->after_cb_wait = get_fpscr() & FPSCR_FLAGS;

    point_now = CB_DELAY;
    sceKernelNotifyCallback(cb, 1);
    sceKernelDelayThreadCB(1000);
    s->after_cb_delay = get_fpscr() & FPSCR_FLAGS;

    sceKernelDeleteCallback(cb);
    return 0;
}

// Sets the flags of an unordered compare, which are C and V, and signals the subject
static int other_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    steps[run].other = compare_flags(compares[3]);
    sceKernelSignalSema(sema, 1);
    return 0;
}

static void print_point(const char *name, int r, int point) {
    PRINTF(seen[r][point].full, "%s: %s: the FPSCR", name, point_names[point]);
    PRINTF(seen[r][point].flags, "%s: %s: the flags", name, point_names[point]);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    subject = sceKernelCreateThread("flags", subject_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    const SceUID id = sceKernelRegisterThreadEventHandler("flags", subject, SCE_KERNEL_THREAD_EVENT_TYPE_START | SCE_KERNEL_THREAD_EVENT_TYPE_EXIT, subject_handler, NULL);
    PRINT("register the handler", test_created(id));

    for (int r = 0; r < RUNS; r++) {
        run = r;
        sceKernelStartThread(subject, 0, NULL);
        TEST_WAIT_FOR(test_in_wait(subject, WAIT_TYPE_SEMA, sema));
        const SceUID other = sceKernelCreateThread("other", other_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
        sceKernelStartThread(other, 0, NULL);
        test_join_thread(other);
        sceKernelWaitThreadEnd(subject, NULL, NULL);
    }

    for (int r = 0; r < RUNS; r++) {
        const char *name = run_names[r];
        const Steps *s = &steps[r];
        print_point(name, r, START_HANDLER);
        print_point(name, r, ENTRY);
        PRINTF(s->dzc, "%s: after a division by zero: the flags", name);
        PRINTF(s->ofc, "%s: after a float overflow: the flags", name);
        PRINTF(s->qc, "%s: after a saturating NEON add: the flags", name);
        PRINTF(s->unordered, "%s: after a compare that is unordered: the flags", name);
        PRINTF(s->greater, "%s: after a compare that is greater: the flags", name);
        PRINTF(s->equal, "%s: after a compare that is equal: the flags", name);
        PRINTF(s->less, "%s: after a compare that is less: the flags", name);
        PRINTF(s->after_delay, "%s: after a delay: the flags", name);
        PRINTF(s->after_wait, "%s: after a wait with another thread on its core: the flags", name);
        PRINTF(s->other, "%s: the other thread's flags after its compare", name);
        print_point(name, r, CB_CHECK);
        PRINTF(s->after_check, "%s: after the callback run by a check: the flags", name);
        print_point(name, r, CB_WAIT);
        PRINTF(s->after_cb_wait, "%s: after the callback run by a wait: the flags", name);
        print_point(name, r, CB_DELAY);
        PRINTF(s->after_cb_delay, "%s: after the callback run by a delay: the flags", name);
        print_point(name, r, END_HANDLER);
    }

    if (id >= 0)
        sceKernelUnregisterThreadEventHandler(id);
    sceKernelDeleteThread(subject);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
