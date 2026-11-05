// The calls that take an object without waiting, in states of the object the other tests don't
// reach: that a failed one leaves the object as it was, whether one gets past a thread that already
// waits, and whether one right after a release takes what the release gave to a waiter. The waiters
// have a lower priority than the main thread and share its core, so they only run once it blocks.
// A waiter that gets a lock keeps it until the main thread signals the release semaphore, so a try
// right after an unlock finds the lock with the waiter even if the waiter already ran. Also the
// vector forms of the message pipe calls that don't wait.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define LOW_PRIORITY (SCE_KERNEL_DEFAULT_PRIORITY_USER + 2)

enum Wait { WAIT_SEMA_1, WAIT_SEMA_2, WAIT_FLAG, WAIT_LW_MUTEX, HOLD_LW_MUTEX, WAIT_READ, WAIT_WRITE, WAIT_RECEIVE, WAIT_SEND, WAIT_EVENT };

static SceUID sema, flag, rwlock, pipe, event, release;
static SceKernelLwMutexWork lw_mutex;
static volatile int wait_kind, wait_result;
static char fill[4096];

// Waits until the main thread signals the release semaphore
static void hold(void) {
    SceUInt timeout = TEST_WAIT_US;
    sceKernelWaitSema(release, 1, &timeout);
}

static int waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    unsigned int bits;
    SceUInt32 pattern;
    SceUInt64 user_data;
    char buf[8];
    int result = 0;
    switch (wait_kind) {
    case WAIT_SEMA_1: result = sceKernelWaitSema(sema, 1, &timeout); break;
    case WAIT_SEMA_2: result = sceKernelWaitSema(sema, 2, &timeout); break;
    case WAIT_FLAG: result = sceKernelWaitEventFlag(flag, 0x2, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR, &bits, &timeout); break;
    case WAIT_LW_MUTEX:
        result = sceKernelLockLwMutex(&lw_mutex, 1, &timeout);
        if (result == 0) {
            hold();
            sceKernelUnlockLwMutex(&lw_mutex, 1);
        }
        break;
    case HOLD_LW_MUTEX:
        result = sceKernelLockLwMutex(&lw_mutex, 1, NULL);
        hold();
        sceKernelUnlockLwMutex(&lw_mutex, 1);
        break;
    case WAIT_READ:
        result = sceKernelLockReadRWLock(rwlock, &timeout);
        if (result == 0) {
            hold();
            sceKernelUnlockReadRWLock(rwlock);
        }
        break;
    case WAIT_WRITE:
        result = sceKernelLockWriteRWLock(rwlock, &timeout);
        if (result == 0) {
            hold();
            sceKernelUnlockWriteRWLock(rwlock);
        }
        break;
    case WAIT_RECEIVE: result = sceKernelReceiveMsgPipe(pipe, buf, 8, MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case WAIT_SEND: result = sceKernelSendMsgPipe(pipe, "12345678", 8, MSG_PIPE_MODE_FULL, NULL, &timeout); break;
    case WAIT_EVENT: result = sceKernelWaitEvent(event, EVENT_BIT_A, &pattern, &user_data, &timeout); break;
    }
    wait_result = result;
    return 0;
}

// Starts a waiter of the kind at a lower priority on the main thread's core, and returns once it waits
static SceUID start_waiter(int kind) {
    wait_kind = kind;
    wait_result = -1;
    const SceUID thread = sceKernelCreateThread("waiter", waiter, LOW_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(thread, 0, NULL);
    test_wait_until_waiting(thread);
    return thread;
}

static int sema_count(void) {
    SceKernelSemaInfo info = { .size = sizeof(info) };
    sceKernelGetSemaInfo(sema, &info);
    return info.currentCount;
}

static unsigned int flag_pattern(void) {
    SceKernelEventFlagInfo info = { .size = sizeof(info) };
    sceKernelGetEventFlagInfo(flag, &info);
    return info.currentPattern;
}

static void test_sema(void) {
    sema = sceKernelCreateSema("sema", 0, 1, 3, NULL);
    PRINT("semaphore: poll 1 of 1", sceKernelPollSema(sema, 1));
    PRINT("semaphore: poll 1 of 0", sceKernelPollSema(sema, 1));
    sceKernelSignalSema(sema, 2);
    PRINT("semaphore: poll 3 of 2", sceKernelPollSema(sema, 3));
    PRINT("semaphore: the count after the failed poll", sema_count());
    sceKernelPollSema(sema, 2);

    // A waiter for 2 is queued when 1 is there
    SceUID thread = start_waiter(WAIT_SEMA_2);
    sceKernelSignalSema(sema, 1);
    PRINT("semaphore: a waiter for 2 still waits with 1 there", test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    PRINT("semaphore: poll 1 with a waiter for 2 queued", sceKernelPollSema(sema, 1));
    PRINT("semaphore: the count after", sema_count());
    PRINT("semaphore: the waiter still waits", test_in_wait(thread, WAIT_TYPE_SEMA, sema));
    sceKernelSignalSema(sema, 2 - sema_count());
    test_join_thread(thread);
    PRINT("semaphore: the waiter for 2 got it after", wait_result);

    // A signal of 1 for a waiter for 1, then a poll before the waiter runs
    thread = start_waiter(WAIT_SEMA_1);
    sceKernelSignalSema(sema, 1);
    const int poll = sceKernelPollSema(sema, 1);
    const int count = sema_count();
    PRINT("semaphore: poll right after a signal for a waiter", poll);
    PRINT("semaphore: the count after", count);
    if (poll == 0)
        sceKernelSignalSema(sema, 1);
    test_join_thread(thread);
    PRINT("semaphore: the waiter", wait_result);
    sceKernelDeleteSema(sema);
}

static void test_flag(void) {
    unsigned int bits = 0xDEAD;
    flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    PRINT("event flag: a failed poll that clears", sceKernelPollEventFlag(flag, 0x3, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR, &bits));
    PRINT("event flag: the bits it reports", bits);
    PRINT("event flag: the pattern after it", flag_pattern());

    // A flag without the multi attribute that a thread waits on
    SceUID thread = start_waiter(WAIT_FLAG);
    bits = 0xDEAD;
    PRINT("event flag: poll while another thread waits", sceKernelPollEventFlag(flag, 0x1, SCE_EVENT_WAITOR, &bits));
    PRINT("event flag: the bits it reports", bits);
    sceKernelSetEventFlag(flag, 0x2);
    bits = 0xDEAD;
    const int poll = sceKernelPollEventFlag(flag, 0x2, SCE_EVENT_WAITOR, &bits);
    const unsigned int pattern = flag_pattern();
    PRINT("event flag: poll right after a set for a waiter that clears", poll);
    PRINT("event flag: the bits it reports", bits);
    PRINT("event flag: the pattern after", pattern);
    test_join_thread(thread);
    PRINT("event flag: the waiter", wait_result);
    sceKernelDeleteEventFlag(flag);
}

static void test_mutex(void) {
    const SceUID mutex = sceKernelCreateMutex("recursive", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
    PRINT("mutex: try lock 2 of a free recursive mutex", sceKernelTryLockMutex(mutex, 2));
    PRINT("mutex: try lock past the largest count", sceKernelTryLockMutex(mutex, 0x7FFFFFFF));
    MutexInfo info = { .size = sizeof(info) };
    sceKernelGetMutexInfo(mutex, (SceKernelMutexInfo *)&info);
    PRINT("mutex: the count after the failed try lock", info.currentCount);
    sceKernelDeleteMutex(mutex);
}

static void test_lw_mutex(void) {
    SceKernelLwMutexWork before;
    sceKernelCreateLwMutex(&lw_mutex, "lw mutex", 0, 0, NULL);
    PRINT("lightweight mutex: try lock a free one", sceKernelTryLockLwMutex(&lw_mutex, 1));
    sceClibMemcpy(&before, &lw_mutex, sizeof(before));
    PRINT("lightweight mutex: try lock it again", sceKernelTryLockLwMutex(&lw_mutex, 1));
    PRINT("lightweight mutex: the failed try lock leaves the work area", sceClibMemcmp(&before, &lw_mutex, sizeof(before)) == 0);
    sceKernelUnlockLwMutex(&lw_mutex, 1);

    // Another thread holds it
    SceUID thread = start_waiter(HOLD_LW_MUTEX);
    sceClibMemcpy(&before, &lw_mutex, sizeof(before));
    PRINT("lightweight mutex: try lock one another thread holds", sceKernelTryLockLwMutex(&lw_mutex, 1));
    PRINT("lightweight mutex: the failed try lock leaves the work area", sceClibMemcmp(&before, &lw_mutex, sizeof(before)) == 0);
    sceKernelSignalSema(release, 1);
    test_join_thread(thread);

    // An unlock with a waiter, then a try lock before the waiter runs
    sceKernelLockLwMutex(&lw_mutex, 1, NULL);
    thread = start_waiter(WAIT_LW_MUTEX);
    sceKernelUnlockLwMutex(&lw_mutex, 1);
    const int try_lock = sceKernelTryLockLwMutex(&lw_mutex, 1);
    PRINT("lightweight mutex: try lock right after an unlock with a waiter", try_lock);
    if (try_lock == 0)
        sceKernelUnlockLwMutex(&lw_mutex, 1);
    sceKernelSignalSema(release, 1);
    test_join_thread(thread);
    PRINT("lightweight mutex: the waiter", wait_result);
    sceKernelDeleteLwMutex(&lw_mutex);
}

static void test_rwlock(void) {
    rwlock = sceKernelCreateRWLock("rwlock", 0, NULL);

    // A write unlock with a waiting reader, then tries before the reader runs
    sceKernelLockWriteRWLock(rwlock, NULL);
    SceUID thread = start_waiter(WAIT_READ);
    sceKernelUnlockWriteRWLock(rwlock);
    const int try_write = sceKernelTryLockWriteRWLock(rwlock);
    if (try_write == 0)
        sceKernelUnlockWriteRWLock(rwlock);
    const int try_read = sceKernelTryLockReadRWLock(rwlock);
    if (try_read == 0)
        sceKernelUnlockReadRWLock(rwlock);
    PRINT("reader/writer lock: try write lock right after a write unlock with a waiting reader", try_write);
    PRINT("reader/writer lock: try read lock right after it", try_read);
    sceKernelSignalSema(release, 1);
    test_join_thread(thread);
    PRINT("reader/writer lock: the reader", wait_result);

    // A read unlock with a waiting writer, then a try before the writer runs
    sceKernelLockReadRWLock(rwlock, NULL);
    thread = start_waiter(WAIT_WRITE);
    sceKernelUnlockReadRWLock(rwlock);
    const int try_again = sceKernelTryLockReadRWLock(rwlock);
    if (try_again == 0)
        sceKernelUnlockReadRWLock(rwlock);
    PRINT("reader/writer lock: try read lock right after a read unlock with a waiting writer", try_again);
    sceKernelSignalSema(release, 1);
    test_join_thread(thread);
    PRINT("reader/writer lock: the writer", wait_result);
    sceKernelDeleteRWLock(rwlock);
}

static void test_pipe(void) {
    char buf[8] = { 0 };
    SceSize size;
    int senders, receivers;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, sizeof(fill), NULL);

    // A receiver waits for 8 bytes, and 4 arrive
    SceUID thread = start_waiter(WAIT_RECEIVE);
    size = 0xDEAD;
    PRINT("message pipe: try send 4 with a receiver waiting for 8", sceKernelTrySendMsgPipe(pipe, "abcd", 4, MSG_PIPE_MODE_FULL, &size));
    PRINT("message pipe: the size sent", size);
    PRINT("message pipe: the receiver still waits", wait_result == -1);
    size = 0xDEAD;
    PRINT("message pipe: try receive 2 with that receiver waiting", sceKernelTryReceiveMsgPipe(pipe, buf, 2, MSG_PIPE_MODE_ASAP, &size));
    PRINT("message pipe: the size received", size);
    PRINT("message pipe: what it received", size == 2 ? (buf[0] << 8) | buf[1] : 0);
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    test_join_thread(thread);
    PRINT("message pipe: the receiver after a cancel", wait_result);
    sceKernelTryReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_ASAP, &size);

    // A sender waits for room for 8 bytes, with 4 free
    sceKernelTrySendMsgPipe(pipe, fill, sizeof(fill) - 4, MSG_PIPE_MODE_FULL, &size);
    thread = start_waiter(WAIT_SEND);
    size = 0xDEAD;
    PRINT("message pipe: try send 2 with a sender waiting for room", sceKernelTrySendMsgPipe(pipe, "xy", 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("message pipe: the size sent", size);
    PRINT("message pipe: the sender still waits", wait_result == -1);
    sceKernelCancelMsgPipe(pipe, &senders, &receivers);
    test_join_thread(thread);
    PRINT("message pipe: the sender after a cancel", wait_result);

    // The vector forms
    char part_a[3] = { 'a', 'b', 'c' }, part_b[5] = { 'd', 'e', 'f', 'g', 'h' }, out_a[4], out_b[4];
    const MsgPipeVector send_vectors[] = { { part_a, sizeof(part_a) }, { part_b, sizeof(part_b) } };
    const MsgPipeVector receive_vectors[] = { { out_a, sizeof(out_a) }, { out_b, sizeof(out_b) } };
    size = 0xDEAD;
    PRINT("message pipe: try receive a vector on an empty pipe", sceKernelTryReceiveMsgPipeVector(pipe, receive_vectors, 2, MSG_PIPE_MODE_ASAP, &size));
    PRINT("message pipe: its size", size);
    size = 0xDEAD;
    PRINT("message pipe: try send a vector", sceKernelTrySendMsgPipeVector(pipe, send_vectors, 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("message pipe: its size", size);
    size = 0xDEAD;
    PRINT("message pipe: try receive a vector", sceKernelTryReceiveMsgPipeVector(pipe, receive_vectors, 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("message pipe: its size", size);
    PRINT("message pipe: the data is in order across the parts", out_a[0] == 'a' && out_a[3] == 'd' && out_b[0] == 'e' && out_b[3] == 'h');
    sceKernelTrySendMsgPipe(pipe, "xyz", 3, MSG_PIPE_MODE_FULL, &size);
    size = 0xDEAD;
    PRINT("message pipe: try receive a vector of 8 with 3 there", sceKernelTryReceiveMsgPipeVector(pipe, receive_vectors, 2, MSG_PIPE_MODE_FULL, &size));
    PRINT("message pipe: its size", size);
    PRINT("message pipe: the 3 are still there", sceKernelTryReceiveMsgPipe(pipe, buf, 3, MSG_PIPE_MODE_FULL, &size));
    sceKernelTrySendMsgPipe(pipe, fill, sizeof(fill), MSG_PIPE_MODE_FULL, &size);
    size = 0xDEAD;
    PRINT("message pipe: try send a vector on a full pipe", sceKernelTrySendMsgPipeVector(pipe, send_vectors, 2, MSG_PIPE_MODE_ASAP, &size));
    PRINT("message pipe: its size", size);
    sceKernelDeleteMsgPipe(pipe);
}

static void test_event(void) {
    SceUInt32 pattern = 0xDEAD;
    SceUInt64 user_data;
    event = sceKernelCreateSimpleEvent("event", EVENT_ATTR_AUTO_RESET, 0, NULL);
    const SceUID thread = start_waiter(WAIT_EVENT);
    sceKernelSetEvent(event, EVENT_BIT_A, 0);
    const int poll = sceKernelPollEvent(event, EVENT_BIT_A, &pattern, &user_data);
    PRINT("simple event: poll an auto reset event right after a set for a waiter", poll);
    PRINT("simple event: the pattern it reports", pattern);
    if (poll == 0)
        sceKernelSetEvent(event, EVENT_BIT_A, 0);
    test_join_thread(thread);
    PRINT("simple event: the waiter", wait_result);
    sceKernelDeleteSimpleEvent(event);
}

int main(void) {
    const SceUID self = sceKernelGetThreadId();
    const int affinity = sceKernelGetThreadCpuAffinityMask(self);
    sceKernelChangeThreadCpuAffinityMask(self, SCE_KERNEL_CPU_MASK_USER_0);
    release = sceKernelCreateSema("release", 0, 0, 1, NULL);

    test_sema();
    test_flag();
    test_mutex();
    test_lw_mutex();
    test_rwlock();
    test_pipe();
    // Last, so a hang on it loses nothing else
    test_event();

    sceKernelDeleteSema(release);
    sceKernelChangeThreadCpuAffinityMask(self, affinity);
    test_exit();
    return 0;
}
