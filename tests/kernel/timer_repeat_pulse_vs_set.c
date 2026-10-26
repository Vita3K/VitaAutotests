// What a repeating timer leaves for a poll or a wait when its events come while no thread waits,
// for a set event and for a pulse event. Each case uses a fresh manual reset timer with an event
// that repeats every 800 ms, which fires at 0.8 s, 1.6 s, 2.4 s and so on from its start. The main
// thread sleeps to times between the fires, at least 300 ms from each. The cases:
// - a set event: a poll at 8.4 s, ten intervals after the start, gets the bit, and a second poll at
//   once gets it again, since a manual reset set event keeps its bit. A clear removes the bit, so
//   the next poll fails, and a poll ten intervals after the clear gets the bit again, since a later
//   fire set it. The polls and the clear between two fires print only after the last of them.
// - a pulse event: a poll at 1.1 s fails, since a pulse leaves no bit. A wait gets the fire at
//   1.6 s, and a poll right after the wait fails. A second wait gets the fire at 2.4 s.
// Each prints the results and the bits, and the elapsed times of the waits as comparisons. The
// pulse case prints only after its last wait.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define INTERVAL_US 800000
#define WAIT_US 2000000
#define NOT_DONE 0xDEAD

// Sleeps until the process time reaches until, in microseconds
static void sleep_until(SceUInt64 until) {
    while (sceKernelGetProcessTimeWide() < until)
        sceKernelDelayThread(1000);
}

// Creates a manual reset timer whose event of the given type repeats every 800 ms
static SceUID create_timer(const char *name, int type) {
    SceUInt64 interval = INTERVAL_US;
    const SceUID timer = sceKernelCreateTimer(name, EVENT_ATTR_MANUAL_RESET, NULL);
    PRINTF(sceKernelSetTimerEvent(timer, type, &interval, 1), "%s: set a repeating event", name);
    return timer;
}

// A poll's result and the bits it got
typedef struct Poll {
    int result;
    SceUInt32 bits;
} Poll;

// Polls the timer for its bit
static Poll poll_timer(SceUID timer) {
    Poll poll = { .bits = NOT_DONE };
    SceUInt64 user_data = 0;
    poll.result = sceKernelPollEvent(timer, EVENT_TIMER, &poll.bits, &user_data);
    return poll;
}

// Prints a poll's result and the bits it got
static void print_result(const char *name, Poll poll) {
    PRINT(name, poll.result);
    PRINTF(poll.bits, "%s: the bits it got", name);
}

// Polls the timer for its bit, and prints the result and the bits the poll got
static void print_poll(const char *name, SceUID timer) {
    print_result(name, poll_timer(timer));
}

// Waits for the timer's bit with a timeout of 2 s. Returns the result, and sets the bits the wait
// got and how long the wait took.
static int wait_for_bit(SceUID timer, SceUInt32 *bits, SceUInt64 *elapsed) {
    SceUInt64 user_data = 0;
    SceUInt32 timeout = WAIT_US;
    const SceUInt64 begin = sceKernelGetProcessTimeWide();
    const int result = sceKernelWaitEvent(timer, EVENT_TIMER, bits, &user_data, &timeout);
    *elapsed = sceKernelGetProcessTimeWide() - begin;
    return result;
}

// A set event: a manual reset timer keeps the bit after a poll, and a clear removes it
static void set_case(void) {
    const SceUID timer = create_timer("set event", TIMER_TYPE_SET_EVENT);
    const SceUInt64 origin = sceKernelGetProcessTimeWide();
    PRINT("set event: start the timer", sceKernelStartTimer(timer));
    sleep_until(origin + 10 * INTERVAL_US + INTERVAL_US / 2);
    const Poll first = poll_timer(timer);
    const Poll second = poll_timer(timer);
    const int cleared = sceKernelClearEvent(timer, 0);
    const Poll after_clear = poll_timer(timer);
    const SceUInt64 clear_time = sceKernelGetProcessTimeWide();
    print_result("set event: the first poll at 8.4 s", first);
    print_result("set event: the second poll at once", second);
    PRINT("set event: clear the timer bit", cleared);
    print_result("set event: the poll after the clear", after_clear);
    sleep_until(clear_time + 10 * INTERVAL_US);
    print_poll("set event: the poll ten intervals after the clear", timer);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// A pulse event: a poll between the fires fails, and each wait gets the next fire
static void pulse_case(void) {
    const SceUID timer = create_timer("pulse event", TIMER_TYPE_PULSE_EVENT);
    const SceUInt64 origin = sceKernelGetProcessTimeWide();
    PRINT("pulse event: start the timer", sceKernelStartTimer(timer));
    sleep_until(origin + 1100000);
    print_poll("pulse event: the poll at 1.1 s", timer);

    SceUInt32 bits = 0;
    SceUInt64 elapsed = 0;
    const int first = wait_for_bit(timer, &bits, &elapsed);
    const SceUInt32 first_bits = bits;
    const SceUInt64 first_elapsed = elapsed;
    SceUInt32 poll_bits = NOT_DONE;
    SceUInt64 poll_user_data = 0;
    const int polled = sceKernelPollEvent(timer, EVENT_TIMER, &poll_bits, &poll_user_data);

    bits = 0;
    const int second = wait_for_bit(timer, &bits, &elapsed);

    // Everything is printed after the second wait, since a print costs tens of milliseconds on the
    // console and would eat into the time the second wait has before the fire
    PRINT("pulse event: the first wait", first);
    PRINT("pulse event: the first wait's bits", first_bits);
    PRINT("pulse event: the first wait returned after over 250 ms", first_elapsed > 250000);
    PRINT("pulse event: the poll right after the first wait", polled);
    PRINT("pulse event: the poll right after the first wait: the bits it got", poll_bits);
    PRINT("pulse event: the second wait", second);
    PRINT("pulse event: the second wait's bits", bits);
    PRINT("pulse event: the second wait returned after over 550 ms", elapsed > 550000);
    PRINT("pulse event: the second wait returned under 1.05 s", elapsed < 1050000);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    set_case();
    pulse_case();

    test_exit();
    return 0;
}
