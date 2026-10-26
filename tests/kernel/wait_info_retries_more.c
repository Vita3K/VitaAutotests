// What sceKernelGetThreadInfo reports for a waiting thread after its object changed in a way that
// doesn't end its wait, and what a waiter reports once its wait ended but before it ran again. The
// model wait_info_retries covers the event flag, semaphore, message pipe and mutex kinds. This
// covers the read lock, write lock, simple event, timer, lightweight mutex, condition variable,
// thread end and thread signal kinds. Every thread is pinned to core 0 and the waiters have a lower
// priority than the main thread, so a waiter the kernel made ready can't run and change what it
// reports until the main thread blocks. Each state is read first and printed later, since printing
// lets the waiters run. Cases:
// - a read lock waiter behind a writer, after the read unlock that leaves the writer holding the
//   lock, then after the writer's unlock
// - a write lock waiter, after one of two read locks is unlocked, then after the second unlock
// - a simple event waiter for a bit, after a set of another bit, then after a set of that bit
// - a timer waiter for a user bit, after the timer's set event fired for 0x8000, which ends the
//   wait by its timeout, since nothing sets the user bit
// - a lightweight mutex waiter behind the one that got the lock, then after that one unlocked
// - a condition variable waiter, after a signal to another waiter, then after the other waiter got
//   the mutex back
// - a thread end waiter, after the target changed its priority, then after the target ended
// - a thread signal waiter, after a signal to another thread, then after a signal to the waiter
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// Lower than the main thread's 0xA0
#define LOW_PRIORITY 0xB0
// Still lower than the main thread, and higher than LOW_PRIORITY
#define CHANGED_PRIORITY 0xA8
// The timer's set event fires this long after the timer starts, well before a wait times out
#define FIRE_US 100000

typedef struct State {
    int status, wait_type, on_object;
} State;

// The status of a thread in a wait
#define STATUS_WAITING 0x8

static volatile int result[2];
static SceUID rwlock, event, cond, cond_mutex, target, release;
static SceKernelLwMutexWork lw_mutex;
static volatile SceUInt32 event_bits;

// Reads what thread reports about its wait, and whether it waits on object
static State state_of(SceUID thread, SceUID object) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return (State){ .status = info.status, .wait_type = info.waitType, .on_object = info.waitId == object };
}

static void print_status(const char *name, State state) {
    PRINTF(state.status, "%s: its status", name);
    PRINTF(state.wait_type, "%s: its wait type", name);
}

static void print_state(const char *name, State state) {
    print_status(name, state);
    PRINTF(state.on_object, "%s: it reports the object as what it waits on", name);
}

// Prints a state after the wait ended. The exact status of a thread that was just woken can change
// between runs, so this prints only that it isn't waiting, and its wait type and object.
static void print_ended(const char *name, State state) {
    PRINTF(state.status != STATUS_WAITING, "%s: it isn't waiting any more", name);
    PRINTF(state.wait_type, "%s: its wait type", name);
    PRINTF(state.on_object, "%s: it reports the object as what it waits on", name);
}

// The same for a thread signal wait, which has no object to report
static void print_signal_ended(const char *name, State state) {
    PRINTF(state.status != STATUS_WAITING, "%s: it isn't waiting any more", name);
    PRINTF(state.wait_type, "%s: its wait type", name);
}

// Takes the write lock into slot i, and unlocks it if it got it
static int write_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    result[i] = sceKernelLockWriteRWLock(rwlock, &timeout);
    if (result[i] == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    return 0;
}

// Takes the read lock into slot i, and unlocks it if it got it
static int read_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    result[i] = sceKernelLockReadRWLock(rwlock, &timeout);
    if (result[i] == 0)
        sceKernelUnlockReadRWLock(rwlock);
    return 0;
}

// Waits for EVENT_BIT_A of event, a simple event or a timer, into slot 0
static int event_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 bits = 0;
    SceUInt64 user_data;
    SceUInt32 timeout = TEST_WAIT_US;
    result[0] = sceKernelWaitEvent(event, EVENT_BIT_A, &bits, &user_data, &timeout);
    event_bits = bits;
    return 0;
}

// Locks the lightweight mutex into slot i, and unlocks it if it got it
static int lw_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    result[i] = sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
    if (result[i] == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    return 0;
}

// Waits on the condition into slot i, holding the mutex, and unlocks it after
static int cond_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelLockMutex(cond_mutex, 1, NULL);
    result[i] = sceKernelWaitCond(cond, &timeout);
    sceKernelUnlockMutex(cond_mutex, 1);
    return 0;
}

// Waits on release, as the target of a thread end wait or the other thread of a signal wait
static int target_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
    return 0;
}

// Waits for the target's end into slot i
static int end_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    result[i] = sceKernelWaitThreadEnd(target, NULL, &timeout);
    return 0;
}

// Waits for a thread signal into slot i
static int signal_waiter(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = TEST_WAIT_US;
    result[i] = sceKernelWaitSignal(0, 0, &timeout);
    return 0;
}

// Starts a low priority waiter on core 0 and returns once it waits
static SceUID start_waiter(SceKernelThreadEntry entry, int i) {
    result[i] = -1;
    const SceUID thread =
        sceKernelCreateThread("waiter", entry, LOW_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, sizeof(i), &i);
    test_wait_until_waiting(thread);
    return thread;
}

static void read_lock_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    const SceUID writer = start_waiter(write_waiter, 0);
    const SceUID reader = start_waiter(read_waiter, 1);
    const State reader_waiting = state_of(reader, rwlock);
    sceKernelUnlockReadRWLock(rwlock);
    const State reader_after_unlock = state_of(reader, rwlock);
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    test_join_thread(writer);
    const State reader_after_writer = state_of(reader, rwlock);
    test_join_thread(reader);

    print_state("read lock, a reader behind a writer", reader_waiting);
    print_state("read lock, a reader after the writer got the lock", reader_after_unlock);
    PRINT("read lock, after the read unlock: the waiting readers", info.numReadWaitThreads);
    print_ended("read lock, the reader after the writer's unlock", reader_after_writer);
    PRINT("read lock: the writer's lock", result[0]);
    PRINT("read lock: the reader's lock", result[1]);
    sceKernelDeleteRWLock(rwlock);
}

static void write_lock_case(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    sceKernelLockReadRWLock(rwlock, NULL);
    const SceUID writer = start_waiter(write_waiter, 0);
    const State waiting = state_of(writer, rwlock);
    sceKernelUnlockReadRWLock(rwlock);
    const State one_reader_left = state_of(writer, rwlock);
    SceKernelRWLockInfo info = { .size = sizeof(info) };
    sceKernelGetRWLockInfo(rwlock, &info);
    sceKernelUnlockReadRWLock(rwlock);
    const State woken = state_of(writer, rwlock);
    test_join_thread(writer);

    print_state("write lock, a writer while two read locks are held", waiting);
    print_state("write lock, a writer after one of two read locks is unlocked", one_reader_left);
    PRINT("write lock, after one read unlock: the waiting writers", info.numWriteWaitThreads);
    print_ended("write lock, a writer after the second read unlock", woken);
    PRINT("write lock: the writer's lock", result[0]);
    sceKernelDeleteRWLock(rwlock);
}

static void simple_event_case(void) {
    event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    const SceUID waiter = start_waiter(event_waiter, 0);
    const State waiting = state_of(waiter, event);
    sceKernelSetEvent(event, EVENT_BIT_B, 0);
    const State other_bit = state_of(waiter, event);
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(event, &info);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    const State woken = state_of(waiter, event);
    test_join_thread(waiter);

    print_state("simple event, waiting for 0x01000000", waiting);
    print_state("simple event, after a set of 0x02000000", other_bit);
    PRINT("simple event, after a set of 0x02000000: the waiting threads", info.numWaitThreads);
    print_ended("simple event, after a set of 0x01000000", woken);
    PRINT("simple event: the wait's result", result[0]);
    PRINT("simple event: the bit it waits for is in the bits it got", (event_bits & EVENT_BIT_A) != 0);
    sceKernelDeleteSimpleEvent(event);
}

static void timer_case(void) {
    event = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET, NULL);
    SceUInt64 interval = FIRE_US;
    sceKernelSetTimerEvent(event, TIMER_TYPE_SET_EVENT, &interval, 0);
    sceKernelStartTimer(event);
    SceUInt32 poll_bits = 0;
    SceUInt64 poll_data;
    TEST_WAIT_FOR(sceKernelPollEvent(event, EVENT_TIMER, &poll_bits, &poll_data) == 0);
    // The waiter starts after the fire, so the fire doesn't change the states read below
    const SceUID waiter = start_waiter(event_waiter, 0);
    const State waiting = state_of(waiter, event);
    EventInfo info = { .size = sizeof(info) };
    sceKernelGetEventInfo(event, &info);
    // Nothing sets the user bit, so the wait ends by its timeout
    test_join_thread(waiter);

    print_state("timer, a waiter for 0x01000000 after its set event fired for 0x8000", waiting);
    PRINT("timer, a waiter for 0x01000000: the waiting threads", info.numWaitThreads);
    PRINT("timer: the wait's result", result[0]);
    sceKernelDeleteTimer(event);
}

static void lw_mutex_case(void) {
    // Created locked by the main thread, so both waiters wait behind it
    sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 1, NULL);
    LwMutexInfo info = { .size = sizeof(info) };
    sceKernelGetLwMutexInfo(&lw_mutex, &info);
    const SceUID object = info.uid;
    const SceUID first = start_waiter(lw_waiter, 0);
    const SceUID second = start_waiter(lw_waiter, 1);
    const State first_waiting = state_of(first, object);
    sceKernelUnlockLwMutex(&lw_mutex, 1);
    const State first_woken = state_of(first, object);
    const State second_behind = state_of(second, object);
    LwMutexInfo after = { .size = sizeof(after) };
    sceKernelGetLwMutexInfo(&lw_mutex, &after);
    test_join_thread(first);
    test_join_thread(second);

    print_state("lightweight mutex, the first waiter", first_waiting);
    print_ended("lightweight mutex, the first waiter after the unlock", first_woken);
    print_state("lightweight mutex, the second waiter behind it", second_behind);
    PRINT("lightweight mutex, after the unlock: the waiting threads", after.numWaitThreads);
    PRINT("lightweight mutex: the first waiter's lock", result[0]);
    PRINT("lightweight mutex: the second waiter's lock", result[1]);
    sceKernelDeleteLwMutex(&lw_mutex);
}

static void cond_case(void) {
    cond_mutex = sceKernelCreateMutex("cond mutex", 0, 0, NULL);
    cond = sceKernelCreateCond("cond", 0, cond_mutex, NULL);
    const SceUID first = start_waiter(cond_waiter, 0);
    const SceUID second = start_waiter(cond_waiter, 1);
    const State first_waiting = state_of(first, cond);
    // The main thread holds the mutex while it signals, so the second waiter only gets it back when
    // the main thread unlocks it
    sceKernelLockMutex(cond_mutex, 1, NULL);
    const int signaled = sceKernelSignalCondTo(cond, second);
    const State first_after_signal = state_of(first, cond);
    sceKernelUnlockMutex(cond_mutex, 1);
    const State second_woken = state_of(second, cond_mutex);
    test_join_thread(second);
    sceKernelSignalCond(cond);
    test_join_thread(first);

    PRINT("condition variable: the signal to the second waiter", signaled);
    print_state("condition variable, the first waiter", first_waiting);
    print_state("condition variable, the first waiter after a signal to the second", first_after_signal);
    print_ended("condition variable, the second waiter after the mutex is unlocked", second_woken);
    PRINT("condition variable: the first waiter's wait", result[0]);
    PRINT("condition variable: the second waiter's wait", result[1]);
    sceKernelDeleteCond(cond);
    sceKernelDeleteMutex(cond_mutex);
}

static void thread_end_case(void) {
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    target = start_waiter(target_thread, 1);
    const SceUID waiter = start_waiter(end_waiter, 0);
    const State waiting = state_of(waiter, target);
    const int changed = sceKernelChangeThreadPriority(target, CHANGED_PRIORITY);
    const State after_change = state_of(waiter, target);
    sceKernelSignalSema(release, 1);
    test_join_thread(target);
    const State woken = state_of(waiter, target);
    test_join_thread(waiter);

    print_state("thread end, a waiter for the target", waiting);
    PRINT("thread end: the target's priority change", changed);
    print_state("thread end, after the target changed its priority", after_change);
    print_ended("thread end, after the target ended", woken);
    PRINT("thread end: the wait's result", result[0]);
    sceKernelDeleteSema(release);
}

// A thread signal waiter reports no object UID that the test can compare, so
// its state is printed without one
static void thread_signal_case(void) {
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);
    const SceUID other = start_waiter(target_thread, 1);
    const SceUID waiter = start_waiter(signal_waiter, 0);
    const State waiting = state_of(waiter, 0);
    const int sent_other = sceKernelSendSignal(other);
    const State after_other = state_of(waiter, 0);
    const int sent = sceKernelSendSignal(waiter);
    const State woken = state_of(waiter, 0);
    test_join_thread(waiter);
    sceKernelSignalSema(release, 1);
    test_join_thread(other);

    print_status("thread signal, a waiter", waiting);
    PRINT("thread signal: the signal to the other thread", sent_other);
    print_status("thread signal, after a signal to the other thread", after_other);
    PRINT("thread signal: the signal to the waiter", sent);
    print_signal_ended("thread signal, after the signal to the waiter", woken);
    PRINT("thread signal: the wait's result", result[0]);
    sceKernelDeleteSema(release);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
    // The main thread only moves to core 0 when it is next scheduled
    sceKernelDelayThread(1000);

    read_lock_case();
    write_lock_case();
    simple_event_case();
    timer_case();
    lw_mutex_case();
    cond_case();
    thread_end_case();
    thread_signal_case();

    test_exit();
    return 0;
}
