// What happens when two threads with the VM attribute call sceKernelSuspendThreadForVM on each
// other at the same time. A runs on core 0 and B on core 1, and both wait on a semaphore that the
// main thread, on core 2, signals twice at once. Each thread then waits for the other to arrive,
// so the two calls start together, stores the call's result and ends with it as its exit status.
// The main thread waits until the threads stop changing, which is when neither can make progress,
// then reads the results and statuses. It resumes both threads and waits for them to end. It
// prints the number of rounds out of 5 where each of these holds:
// - at most one of the two calls returned before the resumes
// - at most one of the two threads is suspended before the resumes
// - at least one resume returned 0, which is a thread that was suspended once
// - both threads ended within 5 s of the resumes
// - at least one thread ended with the exit status 1, which is a suspend that held its target once
//
// On a PS Vita TV on firmware 3.65 in all 5 rounds at most one call returned before the resumes,
// and both threads were suspended, so no round has at most one suspended thread. In all 5 rounds
// a resume returned 0, both threads ended, and a thread ended with the exit status 1.
#include <psp2/kernel/threadmgr.h>
#include <string.h>

#include "test.h"
#include "threadmgr_extra.h"

#define ROUNDS 5
#define SPIN_LIMIT 200000000
// What sceKernelGetThreadInfo reports for a thread that is suspended
#define STATUS_SUSPENDED 0x100

static SceUID sema, thread[2];
static volatile int arrived[2], returned[2];

static int entry(SceSize args, void *argp) {
    (void)args;
    const int me = *(const int *)argp;
    const int other = 1 - me;
    SceUInt32 timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    arrived[me] = 1;
    for (int n = 0; n < SPIN_LIMIT && !arrived[other]; n++)
        ;
    const int result = sceKernelSuspendThreadForVM(thread[other]);
    returned[me] = 1;
    return result;
}

static SceUInt32 status_of(SceUID uid) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    sceKernelGetThreadInfo(uid, &info);
    return info.status;
}

// Returns the thread's exit status, or the error if it didn't end in time
static int wait_end(int i, int *ended) {
    int exit_status = 0;
    SceUInt32 timeout = 5000000;
    const int waited = sceKernelWaitThreadEnd(thread[i], &exit_status, &timeout);
    *ended = waited >= 0;
    return waited < 0 ? waited : exit_status;
}

// Waits until the threads' statuses and whether their calls returned stay the same for 100 ms
static void wait_until_still(void) {
    int last[4] = {-1, -1, -1, -1}, same = 0;
    for (int i = 0; i < 5000 && same < 5; i++) {
        sceKernelDelayThread(20000);
        const int now[4] = {status_of(thread[0]), status_of(thread[1]), returned[0], returned[1]};
        same = memcmp(now, last, sizeof(now)) == 0 ? same + 1 : 0;
        memcpy(last, now, sizeof(now));
    }
}

int main(void) {
    int one_call = 0, one_suspended = 0, one_resume_zero = 0, both_ended = 0, one_held = 0;
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_2);
    // The main thread only moves to core 2 when it is next scheduled
    sceKernelDelayThread(1000);
    for (int round = 0; round < ROUNDS; round++) {
        static int index[2] = {0, 1};
        const SceUInt32 cores[2] = {SCE_KERNEL_CPU_MASK_USER_0, SCE_KERNEL_CPU_MASK_USER_1};
        sema = sceKernelCreateSema("gate", 0, 0, 2, NULL);
        for (int i = 0; i < 2; i++) {
            arrived[i] = returned[i] = 0;
            thread[i] = sceKernelCreateThread("vm", entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, THREAD_ATTR_VM, cores[i], NULL);
        }
        for (int i = 0; i < 2; i++)
            sceKernelStartThread(thread[i], sizeof(int), &index[i]);
        TEST_WAIT_FOR(test_in_wait(thread[0], WAIT_TYPE_SEMA, sema) && test_in_wait(thread[1], WAIT_TYPE_SEMA, sema));
        sceKernelSignalSema(sema, 2);
        wait_until_still();

        const int calls = returned[0] + returned[1];
        const int suspended = (status_of(thread[0]) == STATUS_SUSPENDED) + (status_of(thread[1]) == STATUS_SUSPENDED);
        const int resume0 = sceKernelResumeThreadForVM(thread[0]);
        const int resume1 = sceKernelResumeThreadForVM(thread[1]);
        int ended0, ended1;
        const int exit0 = wait_end(0, &ended0), exit1 = wait_end(1, &ended1);
        one_call += calls <= 1;
        one_suspended += suspended <= 1;
        one_resume_zero += resume0 == 0 || resume1 == 0;
        both_ended += ended0 && ended1;
        one_held += exit0 == 1 || exit1 == 1;
        for (int i = 0; i < 2; i++)
            sceKernelDeleteThread(thread[i]);
        sceKernelDeleteSema(sema);
    }
    PRINTF(one_call, "rounds where at most one call returned before the resumes");
    PRINTF(one_suspended, "rounds where at most one thread was suspended before the resumes");
    PRINTF(one_resume_zero, "rounds where a resume returned 0");
    PRINTF(both_ended, "rounds where both threads ended after the resumes");
    PRINTF(one_held, "rounds where a thread ended with the exit status 1");
    test_exit();
    return 0;
}
