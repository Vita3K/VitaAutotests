// Two threads that each wait for the other's end, each with a timeout of 600 ms. Thread A waits on
// core 0 for thread B, and B waits on core 1 for A. Neither thread ends before both waits are over,
// since each one holds on a semaphore after its wait, so each wait can only end by its timeout. Each
// status variable is preset to 0xDEAD. The test prints both results and whether each status is still
// 0xDEAD, then releases both threads and joins them from the main thread. It is a test of its own,
// since a wrong implementation may hang on the cycle.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CYCLE_US 600000

// One side of the cycle: the thread it waits for, and what its wait returned and wrote
typedef struct CycleSide {
    SceUID other;
    int result;
    int status;
    volatile int done;
} CycleSide;

static CycleSide sides[2];
static const char *const names[] = { "A", "B" };
static SceUID threads[2], begin, hold;

// Waits for the other side's end, then holds until the main thread releases both sides
static int cycle_entry(SceSize args, void *argp) {
    (void)args;
    CycleSide *side = &sides[*(const int *)argp];
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(begin, 1, &timeout);
    timeout = CYCLE_US;
    side->result = sceKernelWaitThreadEnd(side->other, &side->status, &timeout);
    side->done = 1;
    timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(hold, 1, &timeout);
    return 0;
}

int main(void) {
    begin = sceKernelCreateSema("begin", 0, 0, 2, NULL);
    hold = sceKernelCreateSema("hold", 0, 0, 2, NULL);
    for (int i = 0; i < 2; i++) {
        sides[i].result = sides[i].status = 0xDEAD;
        sides[i].done = 0;
        threads[i] = sceKernelCreateThread("cycle", cycle_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0,
                                           i ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_0, NULL);
    }
    sides[0].other = threads[1];
    sides[1].other = threads[0];
    for (int i = 0; i < 2; i++)
        sceKernelStartThread(threads[i], sizeof(i), &i);
    // Both threads are started before either one waits, so neither waits for a thread not yet started
    sceKernelSignalSema(begin, 2);
    TEST_WAIT_FOR(sides[0].done && sides[1].done);

    // Both threads are held on the hold semaphore now, so their state is stable
    PRINT("A waits for B: the result", sides[0].result);
    PRINT("A waits for B: whether its status is still 0xDEAD", sides[0].status == 0xDEAD);
    PRINT("B waits for A: the result", sides[1].result);
    PRINT("B waits for A: whether its status is still 0xDEAD", sides[1].status == 0xDEAD);

    sceKernelSignalSema(hold, 2);
    for (int i = 0; i < 2; i++) {
        SceUInt timeout = TEST_WAIT_US;
        PRINTF(sceKernelWaitThreadEnd(threads[i], NULL, &timeout), "%s: the join after its end", names[i]);
        sceKernelDeleteThread(threads[i]);
    }

    sceKernelDeleteSema(begin);
    sceKernelDeleteSema(hold);
    test_exit();
    return 0;
}
