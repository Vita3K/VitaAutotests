// How many ordinary threads a process can hold at once, and what gives a slot back. Threads that
// never start are created with small stacks until one create fails, which gives the count N and
// the error. A refused create changes nothing, and deleting one thread frees one slot. A thread
// that ended still holds its slot until it is deleted. A thread that calls ExitDeleteThread frees
// its slot once its thread ID no longer resolves. With the slots full, objects of other kinds are
// still created. The count is then checked again with 0x10000 stacks.
// Cases: the error and count at the cap (A), a refused create and one freed slot (B), ended threads
// keep their slots (C), deleted ones free theirs (D), exit deleted threads free theirs (E), 0x10000
// stacks give the same count (F), and other objects at the cap (G).
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Well above the count the test expects, so a loop with no limit still ends
#define CREATE_CAP 256
#define RUNNERS 4
#define SMALL_STACK 0x1000
#define FILL_STACK 0x10000

// The threads this test made and has not deleted, the first held of them
static SceUID threads[CREATE_CAP + 16];
static int held;
static SceUID sema;
static volatile int runner_result[RUNNERS];

static int entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Waits for the semaphore, then ends and deletes its own thread, with its index as the argument.
static int runner(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    runner_result[i] = sceKernelWaitSema(sema, 1, &timeout);
    sceKernelExitDeleteThread(0);
    return 0;
}

static int notify_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    return 0;
}

static int thread_gone(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) < 0;
}

// Returns whether the last RUNNERS held threads all wait on the semaphore
static int all_waiting(void) {
    for (int i = 0; i < RUNNERS; i++) {
        if (!test_in_wait(threads[held - RUNNERS + i], WAIT_TYPE_SEMA, sema))
            return 0;
    }
    return 1;
}

// Creates a thread and keeps it in threads, and returns its UID or the error
static SceUID create(SceKernelThreadEntry thread_entry, SceSize stack_size) {
    const SceUID thread =
        sceKernelCreateThread("limit", thread_entry, SCE_KERNEL_DEFAULT_PRIORITY_USER, stack_size, 0, 0, NULL);
    if (thread >= 0)
        threads[held++] = thread;
    return thread;
}

// Creates threads until one fails or CREATE_CAP are held, and returns how many it made
static int fill(SceKernelThreadEntry thread_entry, SceSize stack_size, SceUID *error) {
    const int before = held;
    *error = 0;
    while (held < CREATE_CAP) {
        const SceUID thread = create(thread_entry, stack_size);
        if (thread < 0) {
            *error = thread;
            break;
        }
    }
    return held - before;
}

// Deletes the last held thread, and returns the result of the delete
static int delete_last(void) {
    return sceKernelDeleteThread(threads[--held]);
}

// Deletes every held thread, and returns how many deletes succeeded
static int delete_all(void) {
    int deleted = 0;
    while (held > 0)
        deleted += delete_last() == 0;
    return deleted;
}

int main(void) {
    sema = sceKernelCreateSema("runners", 0, 0, RUNNERS, NULL);

    // Creating until one fails, or until CREATE_CAP threads are held
    SceUID error = 0;
    const int first = fill(entry, SMALL_STACK, &error);
    PRINT("the error at the cap, 0 if the loop ended first", error);
    PRINT("at least 64 threads created", first >= 64);
    PRINT("fewer than 128 threads created", first < 128);

    // A refused create changes nothing, and one delete frees one slot
    int refused = 0;
    for (int i = 0; i < 3; i++) {
        const SceUID extra = create(entry, SMALL_STACK);
        if (extra < 0)
            refused++;
        else
            delete_last();
    }
    PRINT("three more creates refused", refused == 3);
    PRINT("delete one thread", delete_last());
    const SceUID again = create(entry, SMALL_STACK);
    PRINTF(test_created(again), "create after deleting one");
    const SceUID next = create(entry, SMALL_STACK);
    PRINTF(test_created(next), "the create after that");
    if (next >= 0)
        delete_last();

    // Threads that ended keep their slots until they are deleted
    int started = 0, ended = 0;
    for (int i = 0; i < RUNNERS; i++)
        started += sceKernelStartThread(threads[held - RUNNERS + i], 0, NULL) == 0;
    PRINT("start the last 4 threads", started == RUNNERS);
    for (int i = 0; i < RUNNERS; i++) {
        SceUInt timeout = TEST_WAIT_US;
        ended += sceKernelWaitThreadEnd(threads[held - RUNNERS + i], NULL, &timeout) == 0;
    }
    PRINT("every started thread ends", ended == RUNNERS);
    const SceUID refused_c = create(entry, SMALL_STACK);
    PRINTF(test_created(refused_c), "create while the ended threads are held");
    if (refused_c >= 0)
        delete_last();

    // Deleting them frees their slots, and exactly 4 creates work before one fails
    int deleted = 0;
    for (int i = 0; i < RUNNERS; i++)
        deleted += delete_last() == 0;
    PRINT("delete the ended threads", deleted == RUNNERS);
    SceUID fill_error = 0;
    const int refilled = fill(runner, SMALL_STACK, &fill_error);
    PRINT("exactly 4 creates work after the deletes", refilled == RUNNERS);
    PRINT("the create after those fails with the cap error", fill_error == error);

    // Runners wait on the semaphore, then delete themselves, so their slots come back once they are
    // gone
    int runners_started = 0;
    for (int i = 0; i < RUNNERS; i++) {
        runner_result[i] = 0xDEAD;
        runners_started += sceKernelStartThread(threads[held - RUNNERS + i], sizeof(i), &i) == 0;
    }
    PRINT("start the runners", runners_started == RUNNERS);
    TEST_WAIT_FOR(all_waiting());
    PRINT("every runner waits on the semaphore", all_waiting());
    PRINT("release them", sceKernelSignalSema(sema, RUNNERS));
    for (int i = 0; i < RUNNERS; i++) {
        const SceUID thread = threads[held - RUNNERS + i];
        TEST_WAIT_FOR(thread_gone(thread));
        SceKernelThreadInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelGetThreadInfo(thread, &info), "runner %d after its exit", i);
    }
    int woken = 0;
    for (int i = 0; i < RUNNERS; i++)
        woken += runner_result[i] == 0;
    PRINT("every runner got the semaphore", woken == RUNNERS);
    held -= RUNNERS;

    // Once the runners are gone their slots are free, so a create should work at once. The first
    // one is polled for up to 5 seconds in case it doesn't.
    SceUID reopened_first = create(entry, SMALL_STACK);
    for (int i = 0; reopened_first < 0 && i < 5000; i++) {
        sceKernelDelayThread(1000);
        reopened_first = create(entry, SMALL_STACK);
    }
    PRINTF(test_created(reopened_first), "first create after the exits");
    int reopened = reopened_first >= 0;
    SceUID more_error = 0;
    reopened += fill(entry, SMALL_STACK, &more_error);
    PRINT("exactly 4 creates work after the exits", reopened == RUNNERS);
    PRINT("the create after those fails with the cap error", more_error == error);

    // Everything deleted, then the cap is filled again with bigger stacks
    const int all = held;
    PRINT("delete every thread", delete_all() == all);
    SceUID second_error = 0;
    const int second = fill(entry, FILL_STACK, &second_error);
    PRINT("the error of the second fill, 0 if the loop ended first", second_error);
    PRINT("the second fill creates as many threads as the first", second == first);

    // Objects of other kinds still create at the cap
    const SceUID cap_sema = sceKernelCreateSema("cap", 0, 0, 1, NULL);
    PRINTF(test_created(cap_sema), "semaphore at the cap");
    const SceUID cap_mutex = sceKernelCreateMutex("cap", 0, 0, NULL);
    PRINTF(test_created(cap_mutex), "mutex at the cap");
    const SceUID cap_cb = sceKernelCreateCallback("cap", 0, notify_callback, NULL);
    PRINTF(test_created(cap_cb), "callback at the cap");
    const SceUID cap_timer = sceKernelCreateTimer("cap", 0, NULL);
    PRINTF(test_created(cap_timer), "timer at the cap");
    const SceUID cap_pipe = sceKernelCreateMsgPipe("cap", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    PRINTF(test_created(cap_pipe), "message pipe at the cap");
    const SceUID refused_g = create(entry, SMALL_STACK);
    PRINTF(test_created(refused_g), "thread at the cap");
    if (refused_g >= 0)
        delete_last();
    if (cap_sema >= 0)
        sceKernelDeleteSema(cap_sema);
    if (cap_mutex >= 0)
        sceKernelDeleteMutex(cap_mutex);
    if (cap_cb >= 0)
        sceKernelDeleteCallback(cap_cb);
    if (cap_timer >= 0)
        sceKernelDeleteTimer(cap_timer);
    if (cap_pipe >= 0)
        sceKernelDeleteMsgPipe(cap_pipe);
    PRINT("delete the threads at the cap", delete_all() == second);

    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
