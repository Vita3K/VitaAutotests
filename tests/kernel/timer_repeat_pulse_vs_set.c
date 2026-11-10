// What a repeating timer leaves for a poll or a wait when its events come while no thread waits,
// for a set event and for a pulse event. Each case uses a fresh manual reset timer with an event
// that repeats every 800 ms, which fires at 0.8 s, 1.6 s, 2.4 s and so on from its start. The main
// thread sleeps to times between the fires of the timer it checks, at least 300 ms from each. The
// cases:
// - a set event: a poll at 8.4 s, ten intervals after the start, gets the bit, and a second poll at
//   once gets it again, since a manual reset set event keeps its bit. A clear removes the bit, so
//   the next poll fails, and a poll ten intervals after the clear gets the bit again, since a later
//   fire set it. The polls and the clear between two fires print only after the last of them.
// - a pulse event: a poll at 1.1 s fails, since a pulse leaves no bit. A wait gets the fire at
//   1.6 s, and a poll right after the wait fails. A second wait gets the fire at 2.4 s.
// Each prints the results and the bits, and the elapsed times of the waits as comparisons. The
// pulse case runs while the set case sleeps to its first poll, with its timer started 400 ms after
// the set timer, so the fires of the two timers are 400 ms apart. It prints after the set case.
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

// Creates a manual reset timer whose event of the given type repeats every 800 ms, and returns the
// result of setting the event
static int create_timer(const char *name, int type, SceUID *timer) {
    SceUInt64 interval = INTERVAL_US;
    *timer = sceKernelCreateTimer(name, EVENT_ATTR_MANUAL_RESET, NULL);
    return sceKernelSetTimerEvent(*timer, type, &interval, 1);
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

// What the pulse case got
typedef struct Pulse {
    int set;
    int started;
    Poll early;
    int first;
    SceUInt32 first_bits;
    SceUInt64 first_elapsed;
    Poll after_first;
    int second;
    SceUInt32 second_bits;
    SceUInt64 second_elapsed;
} Pulse;

// A pulse event: a poll between the fires fails, and each wait gets the next fire
static void pulse_case(Pulse *pulse) {
    SceUID timer;
    pulse->set = create_timer("pulse event", TIMER_TYPE_PULSE_EVENT, &timer);
    const SceUInt64 origin = sceKernelGetProcessTimeWide();
    pulse->started = sceKernelStartTimer(timer);
    sleep_until(origin + 1100000);
    pulse->early = poll_timer(timer);
    pulse->first = wait_for_bit(timer, &pulse->first_bits, &pulse->first_elapsed);
    pulse->after_first = poll_timer(timer);
    pulse->second = wait_for_bit(timer, &pulse->second_bits, &pulse->second_elapsed);

    sceKernelStopTimer(timer);
    sceKernelDeleteTimer(timer);
}

// Prints what the pulse case got
static void print_pulse(const Pulse *pulse) {
    PRINT("pulse event: set a repeating event", pulse->set);
    PRINT("pulse event: start the timer", pulse->started);
    print_result("pulse event: the poll at 1.1 s", pulse->early);
    PRINT("pulse event: the first wait", pulse->first);
    PRINT("pulse event: the first wait's bits", pulse->first_bits);
    PRINT("pulse event: the first wait returned after over 250 ms", pulse->first_elapsed > 250000);
    print_result("pulse event: the poll right after the first wait", pulse->after_first);
    PRINT("pulse event: the second wait", pulse->second);
    PRINT("pulse event: the second wait's bits", pulse->second_bits);
    PRINT("pulse event: the second wait returned after over 550 ms", pulse->second_elapsed > 550000);
    PRINT("pulse event: the second wait returned under 1.05 s", pulse->second_elapsed < 1050000);
}

// A set event: a manual reset timer keeps the bit after a poll, and a clear removes it. The pulse
// case runs in the sleep to the first poll and prints at the end.
static void set_case(void) {
    SceUID timer;
    PRINT("set event: set a repeating event", create_timer("set event", TIMER_TYPE_SET_EVENT, &timer));
    const SceUInt64 origin = sceKernelGetProcessTimeWide();
    const int started = sceKernelStartTimer(timer);
    // The pulse timer starts halfway between two set fires, so a pulse wait that a set fire ends
    // returns too early
    sleep_until(origin + INTERVAL_US / 2);
    Pulse pulse = { 0 };
    pulse_case(&pulse);
    PRINT("set event: start the timer", started);
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

    print_pulse(&pulse);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    set_case();

    test_exit();
    return 0;
}
