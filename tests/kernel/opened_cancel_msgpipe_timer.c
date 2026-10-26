// Cancels a message pipe and a timer through one of their two UIDs while a waiter queues through the
// other, and prints the counts and results, the bytes left in the pipe, and the timer's event as a
// poll finds it after the cancel. The model opened_waiters covers the receive and the timer wait
// through both UIDs, and the cancels here are new. The cases:
// - a message pipe with 4 bytes sent through the creating UID, a receive of 8 bytes through the opened
//   UID that waits for the missing bytes, then the cancel through the creating UID
// - a timer with a one shot event 1 second ahead, started through the creating UID, a wait for its
//   event through the creating UID with a timeout of 2 seconds, the cancel through the opened UID, and
//   a poll through the creating UID 1.5 seconds after the cancel
// Every wait has a timeout of 2 seconds, and each waiter is joined before its result is read. The
// timer's event is due 1 second after it starts, so the poll is 0.5 seconds past that.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define PIPE_NAME "Cancel pipe"
#define TIMER_NAME "Cancel timer"
#define ONE_SECOND 1000000
#define POLL_DELAY 1500000

static SceUID wait_uid;
static volatile int wait_result;
static char message[4] = "abc";
static char received[8];

// Receives 8 bytes from the pipe, which only has 4, so the receive waits
static int receiver(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result =
        sceKernelReceiveMsgPipe(wait_uid, received, sizeof(received), MSG_PIPE_MODE_FULL, NULL, &timeout);
    return 0;
}

// Waits for the timer event through wait_uid
static int timer_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = TEST_WAIT_US;
    SceUInt32 pattern;
    SceUInt64 user_data;
    wait_result = sceKernelWaitEvent(wait_uid, EVENT_TIMER, &pattern, &user_data, &timeout);
    return 0;
}

static void pipe_case(void) {
    const char *what = "message pipe, cancel through the creating UID";
    const SceUID creating =
        sceKernelCreateMsgPipe(PIPE_NAME, MSG_PIPE_TYPE_USER, SCE_KERNEL_ATTR_OPENABLE, 4096, NULL);
    const SceUID opened = sceKernelOpenMsgPipe(PIPE_NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "%s: create and open the pipe", what);
    SceSize sent = 0;
    PRINTF(sceKernelTrySendMsgPipe(creating, message, sizeof(message), MSG_PIPE_MODE_ASAP, &sent),
           "%s: send 4 bytes through the creating UID", what);

    wait_uid = opened;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(receiver);
    test_wait_until_waiting(thread);
    SceKernelMppInfo before = {.size = sizeof(before)};
    sceKernelGetMsgPipeInfo(opened, &before);
    int senders = NOT_DONE, receivers = NOT_DONE;
    const int canceled = sceKernelCancelMsgPipe(creating, &senders, &receivers);
    test_join_thread(thread);

    SceKernelMppInfo after = {.size = sizeof(after)};
    sceKernelGetMsgPipeInfo(opened, &after);
    PRINTF(canceled, "%s: the cancel", what);
    PRINTF(senders, "%s: the senders it counted", what);
    PRINTF(receivers, "%s: the receivers it counted", what);
    PRINTF(before.numReceiveWaitThreads, "%s: the receivers the info counted before", what);
    PRINTF(wait_result, "%s: the receiver's result", what);
    PRINTF(after.bufSize - after.freeSize, "%s: the bytes left after, through the opened UID", what);

    sceKernelCloseMsgPipe(opened);
    sceKernelDeleteMsgPipe(creating);
}

static void timer_case(void) {
    const char *what = "timer, cancel through the opened UID";
    const SceUID creating = sceKernelCreateTimer(TIMER_NAME, SCE_KERNEL_ATTR_OPENABLE, NULL);
    const SceUID opened = sceKernelOpenTimer(TIMER_NAME);
    PRINTF(creating < 0 ? creating : opened < 0 ? opened : 0, "%s: create and open the timer", what);
    SceUInt64 interval = ONE_SECOND;
    PRINTF(sceKernelSetTimerEvent(creating, TIMER_TYPE_SET_EVENT, &interval, 0),
           "%s: a one shot event 1 second ahead", what);
    PRINTF(sceKernelStartTimer(creating), "%s: start it", what);

    wait_uid = creating;
    wait_result = NOT_DONE;
    const SceUID thread = test_start_thread(timer_waiter);
    test_wait_until_waiting(thread);
    TimerInfo before = {.size = sizeof(before)};
    sceKernelGetTimerInfo(creating, &before);
    SceUInt32 waiting = NOT_DONE;
    const int canceled = sceKernelCancelTimer(opened, &waiting);
    test_join_thread(thread);
    PRINTF(canceled, "%s: the cancel", what);
    PRINTF(waiting, "%s: the waiters it counted", what);
    PRINTF(before.numWaitThreads, "%s: the waiters the info counted before", what);
    PRINTF(wait_result, "%s: the waiter's wait", what);

    sceKernelDelayThread(POLL_DELAY);
    SceUInt32 bits = NOT_DONE;
    SceUInt64 user_data = 0;
    PRINTF(sceKernelPollEvent(creating, EVENT_TIMER, &bits, &user_data),
           "%s: poll for its event through the creating UID", what);
    PRINTF(bits, "%s: the bits the poll read", what);

    sceKernelStopTimer(creating);
    sceKernelCloseTimer(opened);
    sceKernelDeleteTimer(creating);
}

int main(void) {
    pipe_case();
    timer_case();
    test_exit();
    return 0;
}
