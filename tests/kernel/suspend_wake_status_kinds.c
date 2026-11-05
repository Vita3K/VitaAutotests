// What sceKernelGetThreadInfo reports for a waiter that sceKernelSuspendThreadForVM holds, in the
// calls right after the wake that ends its wait, for each kind of wait. suspend_for_vm_handoff and
// suspend_for_vm_handoff_more wait for W's state to settle before they read it. This test reads it
// at once, with nothing in between, and then again after it settles.
// W has the VM attribute and runs on the main thread's core at a lower priority, so W only runs
// once the main thread waits. The main thread waits until W waits, suspends W and ends its wait.
// It reads W's status and wait type, then what the object holds, and tries to take the object
// itself. It resumes W, joins it and prints W's result and what W got. Kinds:
// - an event flag set, for a waiter with SCE_EVENT_WAITCLEAR_PAT, with a second bit set that W
//   doesn't wait for
// - a mutex unlock, a lightweight mutex unlock
// - a write unlock, for a reader
// - a message pipe send for a waiting receiver, and a message pipe receive that makes room for a
//   waiting sender
// - a simple event set, a timer that the main thread starts, and a thread signal
// - the end of a thread that W waits for
// Each wait has a timeout of several seconds, which a test that works never reaches.
//
// After the first read, the main thread polls until W is neither RUNNING nor READY and waits for
// nothing, for at most 200 ms. It prints that status, the wait type again, and the object. The
// timer is manual reset, so its bit stays set. The thread end target waits for a semaphore. Its
// end can't be forced by another thread, since the Vita has no call to terminate a thread, so
// only the end that comes from its return is here.
//
// On a PS Vita TV on firmware 3.65 W shows READY 0x2 with wait type 0 right after the wake for
// every kind, and SUSPENDED 0x100 with wait type 0 once it has settled. W's result is 0 for
// every kind after the resume. While W is suspended, the objects already hold the result of the
// wake:
// - the event flag has the pattern 0x2, since W's wait cleared bit 0x1, W later gets 0x3, and the
//   flag has no waiter
// - the mutex and the lightweight mutex have W as owner with count 1 and no waiter, and a try
//   lock by the main thread fails with 0x80028145 and 0x80028185
// - the read-write lock has lock count 1 and no waiter, W is not its write owner, and a try write
//   lock fails with 0x800281E6
// - after the send, the pipe has all 0x1000 bytes free and no waiter, and W later gets the
//   message "abcd"
// - after the receive, the pipe has 0xFFC bytes free and no waiter, so the 4 bytes of W's message
//   are already in it, and after the resume it still has 0xFFC free and holds that message
// - the simple event has its bit cleared, since the auto reset took it, and W later gets it
// - the timer event has its bit 0x8000 set and no waiter, and W later gets it
// - the thread end target has the status 0x10 and the exit status 0x3C, and W later gets 0x3C
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAIN_PRIORITY 0x80
#define W_PRIORITY 0xA0
#define PIPE_SIZE 4096
#define TARGET_STATUS 0x3C
#define TIMER_US 100000
#define NOT_SET 0xDEAD

typedef enum Kind {
    FLAG,
    MUTEX,
    LW_MUTEX,
    RW_LOCK,
    PIPE_RECEIVE,
    PIPE_SEND,
    EVENT,
    TIMER,
    SIGNAL,
    THREAD_END
} Kind;

static Kind kind;
static SceUID object, target, target_sema;
static SceKernelLwMutexWork lw_mutex;
static volatile int result, release, status_out, got;
static volatile SceUInt32 bits_out;
static char fill[PIPE_SIZE];

static int target_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    sceKernelWaitSema(target_sema, 1, &timeout);
    return TARGET_STATUS;
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = 3 * TEST_WAIT_US;
    SceUInt32 bits = NOT_SET;
    char buf[4] = { 0 };
    int stat = NOT_SET;
    switch (kind) {
    case FLAG:
        result = sceKernelWaitEventFlag(
            object, 0x1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
        break;
    case MUTEX:
        result = sceKernelLockMutex(object, 1, &timeout);
        release = sceKernelUnlockMutex(object, 1);
        break;
    case LW_MUTEX:
        result = sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
        release = sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case RW_LOCK:
        result = sceKernelLockReadRWLock(object, &timeout);
        release = sceKernelUnlockReadRWLock(object);
        break;
    case PIPE_RECEIVE:
        result =
            sceKernelReceiveMsgPipe(object, buf, sizeof(buf), MSG_PIPE_MODE_FULL, NULL, &timeout);
        got = *(int *)buf;
        break;
    case PIPE_SEND:
        result = sceKernelSendMsgPipe(object, "abcd", 4, MSG_PIPE_MODE_FULL, NULL, &timeout);
        break;
    case EVENT: result = sceKernelWaitEvent(object, EVENT_BIT_A, &bits, NULL, &timeout); break;
    case TIMER: result = sceKernelWaitEvent(object, EVENT_TIMER, &bits, NULL, &timeout); break;
    case SIGNAL: result = sceKernelWaitSignal(0, 0, &timeout); break;
    case THREAD_END:
        result = sceKernelWaitThreadEnd(target, &stat, &timeout);
        status_out = stat;
        break;
    }
    bits_out = bits;
    return 0;
}

static SceUInt32 wait_type_of(Kind k) {
    switch (k) {
    case FLAG: return WAIT_TYPE_EVENT_FLAG;
    case MUTEX: return WAIT_TYPE_MUTEX;
    case LW_MUTEX: return WAIT_TYPE_LW_MUTEX;
    case RW_LOCK: return WAIT_TYPE_RW_LOCK;
    case PIPE_RECEIVE:
    case PIPE_SEND: return WAIT_TYPE_MSG_PIPE;
    case EVENT:
    case TIMER: return WAIT_TYPE_EVENT;
    case SIGNAL: return WAIT_TYPE_SIGNAL;
    case THREAD_END: return WAIT_TYPE_THREAD_END;
    }
    return 0;
}

static int waits_in(SceUID thread, SceUInt32 type) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 && info.waitType == type;
}

static int timer_fired(void) {
    EventInfo info = { .size = sizeof(info) };
    return sceKernelGetEventInfo(object, &info) == 0 && (info.eventPattern & EVENT_TIMER);
}

static int target_ended(void) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(target, &info) == 0 &&
        !(info.status & (SCE_THREAD_RUNNING | SCE_THREAD_READY | SCE_THREAD_WAITING));
}

static void create_object(Kind k) {
    switch (k) {
    case FLAG: object = sceKernelCreateEventFlag("flag", 0, 0, NULL); break;
    case MUTEX:
        object = sceKernelCreateMutex("mutex", 0, 0, NULL);
        sceKernelLockMutex(object, 1, NULL);
        break;
    case LW_MUTEX:
        sceKernelCreateLwMutex(&lw_mutex, "lw", 0, 0, NULL);
        sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        break;
    case RW_LOCK:
        object = sceKernelCreateRWLock("rw", 0, NULL);
        sceKernelLockWriteRWLock(object, NULL);
        break;
    case PIPE_RECEIVE:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
        break;
    case PIPE_SEND:
        object = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, PIPE_SIZE, NULL);
        sceKernelTrySendMsgPipe(object, fill, sizeof(fill), MSG_PIPE_MODE_FULL, NULL);
        break;
    case EVENT: object = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL); break;
    case TIMER: {
        object = sceKernelCreateTimer("timer", EVENT_ATTR_MANUAL_RESET, NULL);
        SceUInt64 interval = TIMER_US;
        sceKernelSetTimerEvent(object, TIMER_TYPE_SET_EVENT, &interval, 0);
        break;
    }
    case SIGNAL: break;
    case THREAD_END:
        target_sema = sceKernelCreateSema("target", 0, 0, 1, NULL);
        target = sceKernelCreateThread(
            "target", target_entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        sceKernelStartThread(target, 0, NULL);
        test_wait_until_waiting(target);
        break;
    }
}

// Waits for cond without blocking, for at most 2 s, so W can't run on this core before the read
static void spin_until(int (*cond)(void)) {
    const SceUInt64 end = sceKernelGetProcessTimeWide() + 2000000;
    while (!cond() && sceKernelGetProcessTimeWide() < end)
        ;
}

// Ends W's wait. For a timer, the timer starts, and the wake is the event 100 ms later, which this
// waits for. For the thread end, it waits for the target to be over.
static int wake(Kind k, SceUID w) {
    SceSize size;
    int r;
    switch (k) {
    case FLAG: return sceKernelSetEventFlag(object, 0x3);
    case MUTEX: return sceKernelUnlockMutex(object, 1);
    case LW_MUTEX: return sceKernelUnlockLwMutex(&lw_mutex, 1);
    case RW_LOCK: return sceKernelUnlockWriteRWLock(object);
    case PIPE_RECEIVE:
        return sceKernelTrySendMsgPipe(object, "abcd", 4, MSG_PIPE_MODE_FULL, &size);
    case PIPE_SEND:
        return sceKernelTryReceiveMsgPipe(object, fill, sizeof(fill), MSG_PIPE_MODE_FULL, &size);
    case EVENT: return sceKernelSetEvent(object, EVENT_BIT_A, 0);
    case TIMER:
        r = sceKernelStartTimer(object);
        spin_until(timer_fired);
        return r;
    case SIGNAL: return sceKernelSendSignal(w);
    case THREAD_END:
        r = sceKernelSignalSema(target_sema, 1);
        spin_until(target_ended);
        return r;
    }
    return 0;
}

// Prints what the object holds while W is suspended, and what a take by the main thread gets.
// Whatever the main thread takes it gives back, so W can still get it after the resume.
static void print_object(const char *name, Kind k, SceUID w) {
    switch (k) {
    case FLAG: {
        SceKernelEventFlagInfo info = { .size = sizeof(info) };
        sceKernelGetEventFlagInfo(object, &info);
        PRINTF(info.currentPattern, "%s: the pattern", name);
        PRINTF(info.numWaitThreads, "%s: the waiters", name);
        break;
    }
    case MUTEX: {
        MutexInfo info = { .size = sizeof(info) };
        sceKernelGetMutexInfo(object, (SceKernelMutexInfo *)&info);
        PRINTF(info.currentOwnerId == w, "%s: W owns the mutex", name);
        PRINTF(info.currentCount, "%s: the mutex's count", name);
        PRINTF(info.numWaitThreads, "%s: the mutex's waiters", name);
        const int try_lock = sceKernelTryLockMutex(object, 1);
        PRINTF(try_lock, "%s: try lock the mutex", name);
        if (try_lock == 0)
            sceKernelUnlockMutex(object, 1);
        break;
    }
    case LW_MUTEX: {
        LwMutexInfo info = { .size = sizeof(info) };
        sceKernelGetLwMutexInfo(&lw_mutex, &info);
        PRINTF(info.currentOwnerId == w, "%s: W owns the lightweight mutex", name);
        PRINTF(info.currentCount, "%s: its count", name);
        PRINTF(info.numWaitThreads, "%s: its waiters", name);
        const int try_lock = sceKernelTryLockLwMutex(&lw_mutex, 1);
        PRINTF(try_lock, "%s: try lock it", name);
        if (try_lock == 0)
            sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    }
    case RW_LOCK: {
        SceKernelRWLockInfo info = { .size = sizeof(info) };
        sceKernelGetRWLockInfo(object, &info);
        PRINTF(info.lockCount, "%s: the lock count", name);
        PRINTF(info.writeOwnerId == w, "%s: W is the write owner", name);
        PRINTF(info.numReadWaitThreads + info.numWriteWaitThreads, "%s: the waiters", name);
        const int try_write = sceKernelTryLockWriteRWLock(object);
        PRINTF(try_write, "%s: try write lock", name);
        if (try_write == 0)
            sceKernelUnlockWriteRWLock(object);
        break;
    }
    case PIPE_RECEIVE:
    case PIPE_SEND: {
        SceKernelMppInfo info = { .size = sizeof(info) };
        sceKernelGetMsgPipeInfo(object, &info);
        PRINTF(info.freeSize, "%s: the free size", name);
        PRINTF(info.numSendWaitThreads, "%s: the senders", name);
        PRINTF(info.numReceiveWaitThreads, "%s: the receivers", name);
        break;
    }
    case EVENT:
    case TIMER: {
        EventInfo info = { .size = sizeof(info) };
        sceKernelGetEventInfo(object, &info);
        PRINTF(info.eventPattern & (k == EVENT ? EVENT_BIT_A : EVENT_TIMER), "%s: the bit", name);
        PRINTF(info.numWaitThreads, "%s: the waiters", name);
        break;
    }
    case SIGNAL: break;
    case THREAD_END: {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        sceKernelGetThreadInfo(target, &info);
        PRINTF(info.status, "%s: the target's status", name);
        PRINTF(info.exitStatus, "%s: the target's exit status", name);
        break;
    }
    }
}

// Prints what W did after it was resumed, which depends on the kind
static void print_after(const char *name, Kind k) {
    switch (k) {
    case FLAG: PRINTF(bits_out, "%s: the pattern W got", name); break;
    case EVENT:
    case TIMER: PRINTF(bits_out & (k == EVENT ? EVENT_BIT_A : EVENT_TIMER), "%s: the bit W got", name); break;
    case PIPE_RECEIVE: {
        PRINTF(got, "%s: the message W got", name);
        break;
    }
    case PIPE_SEND: {
        char buf[4] = { 0 };
        SceKernelMppInfo info = { .size = sizeof(info) };
        sceKernelGetMsgPipeInfo(object, &info);
        PRINTF(info.freeSize, "%s: the free size after W sent", name);
        PRINTF(sceKernelTryReceiveMsgPipe(object, buf, sizeof(buf), MSG_PIPE_MODE_FULL, NULL),
            "%s: receive what W sent", name);
        PRINTF(*(int *)buf, "%s: the message", name);
        break;
    }
    case THREAD_END: PRINTF(status_out, "%s: the exit status W got", name); break;
    default: break;
    }
    if (release != NOT_SET)
        PRINTF(release, "%s: W's release after it", name);
}

static void delete_object(Kind k) {
    switch (k) {
    case FLAG: sceKernelDeleteEventFlag(object); break;
    case MUTEX: sceKernelDeleteMutex(object); break;
    case LW_MUTEX: sceKernelDeleteLwMutex(&lw_mutex); break;
    case RW_LOCK: sceKernelDeleteRWLock(object); break;
    case PIPE_RECEIVE:
    case PIPE_SEND: sceKernelDeleteMsgPipe(object); break;
    case EVENT: sceKernelDeleteSimpleEvent(object); break;
    case TIMER:
        sceKernelStopTimer(object);
        sceKernelDeleteTimer(object);
        break;
    case SIGNAL: break;
    case THREAD_END:
        sceKernelDeleteThread(target);
        sceKernelDeleteSema(target_sema);
        break;
    }
}

static void run_case(const char *name, Kind k) {
    kind = k;
    result = release = status_out = got = NOT_SET;
    bits_out = NOT_SET;
    create_object(k);
    const SceUID w = sceKernelCreateThread(
        "w", waiter, W_PRIORITY, 0x4000, THREAD_ATTR_VM, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    sceKernelStartThread(w, 0, NULL);
    TEST_WAIT_FOR(waits_in(w, wait_type_of(k)));
    const int suspended = sceKernelSuspendThreadForVM(w);
    const int woken = wake(k, w);
    // The next calls, with nothing in between
    SceKernelThreadInfo info = { .size = sizeof(info) };
    const int got_info = sceKernelGetThreadInfo(w, &info);
    PRINTF(suspended, "%s: suspend W", name);
    PRINTF(woken, "%s: the wake", name);
    PRINTF(got_info, "%s: get W's info", name);
    PRINTF(info.status, "%s: W's status", name);
    PRINTF(info.waitType, "%s: W's wait type", name);
    for (int i = 0; i < 200; i++) {
        if (info.waitType == 0 && !(info.status & (SCE_THREAD_RUNNING | SCE_THREAD_READY)))
            break;
        sceKernelDelayThread(1000);
        sceKernelGetThreadInfo(w, &info);
    }
    PRINTF(info.status, "%s: W's status once it settles", name);
    PRINTF(info.waitType, "%s: W's wait type once it settles", name);
    print_object(name, k, w);
    PRINTF(sceKernelResumeThreadForVM(w), "%s: resume W", name);
    test_join_thread(w);
    PRINTF(result, "%s: W's result", name);
    print_after(name, k);
    delete_object(k);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceKernelChangeThreadPriority(0, MAIN_PRIORITY);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    run_case("event flag set", FLAG);
    run_case("mutex unlock", MUTEX);
    run_case("lightweight mutex unlock", LW_MUTEX);
    run_case("write unlock to a reader", RW_LOCK);
    run_case("message pipe send", PIPE_RECEIVE);
    run_case("message pipe receive", PIPE_SEND);
    run_case("simple event set", EVENT);
    run_case("timer event", TIMER);
    run_case("thread signal", SIGNAL);
    run_case("thread end", THREAD_END);

    test_exit();
    return 0;
}
