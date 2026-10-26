// How many DONT_WAIT FULL receives of 4 bytes can be pending on one empty message pipe at once.
// Each call gets its own buffer, size variable and timeout variable, all with static storage, since
// a pending call may write them after it returned. The test makes receives one at a time until one
// fails or 300 calls were made, and prints how many returned 0, how many calls were made and the
// first failure's result (0 if none failed). Then a blocking FULL send of 4 bytes goes to each
// pending receive in turn, the test prints how many of those sends returned 0 and waits for the
// pipe's event as msgpipe_async does. A DONT_WAIT receive never writes its size, so each buffer is
// prefilled with a marker, the test polls the buffers for at most 5 s until every pending receive
// got its 4 bytes, and prints whether the number of buffers that did equals the number of receives
// that returned 0. Last, one more DONT_WAIT
// FULL receive of 4 is made, its result is printed, and a send of 4 bytes ends it. This test is
// alone and last, since many pending calls may strain a console.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_CALLS 300
// The timeout each DONT_WAIT receive is given
#define RECEIVE_TIMEOUT_US 3000000

static SceUID pipe;
static volatile char bufs[MAX_CALLS][4];
static SceSize sizes[MAX_CALLS];
static SceUInt32 timeouts[MAX_CALLS];

// Prints whether the pipe reports the event, with the user data split into its two halves
static void print_event(const char *name, SceUInt32 bit) {
    SceUInt32 pattern = 0xDEAD;
    SceUInt64 user_data = 0xDEADDEADDEADDEADull;
    SceUInt32 timeout = 100000;
    PRINTF(sceKernelWaitEvent(pipe, bit, &pattern, &user_data, &timeout), "%s: wait for the event", name);
    PRINTF(pattern, "%s: the event's bits", name);
    PRINTF((int)(user_data & 0xFFFFFFFF), "%s: the low half of its user data", name);
    PRINTF((int)(user_data >> 32), "%s: the high half of its user data", name);
}

// Returns how many of the first n buffers hold the piece the sends gave
static int count_filled(const char *piece, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (bufs[i][0] == piece[0] && bufs[i][1] == piece[1] && bufs[i][2] == piece[2] && bufs[i][3] == piece[3])
            count++;
    }
    return count;
}

int main(void) {
    static const char piece[4] = { 'a', 'b', 'c', 'd' };
    char last_buf[4] = { 0 };
    SceSize last_size = 0xDEAD;
    SceUInt32 last_timeout = RECEIVE_TIMEOUT_US;
    SceSize size;
    SceUInt32 timeout;

    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);

    // Receives, one call at a time, until one fails or MAX_CALLS were made. The calls before a
    // failure all returned 0, so bufs[0] to bufs[zeros - 1] are the pending receives.
    int calls = 0, zeros = 0, first_failure = 0;
    while (calls < MAX_CALLS) {
        sizes[calls] = 0xDEAD;
        for (int i = 0; i < 4; i++)
            bufs[calls][i] = 'x';
        timeouts[calls] = RECEIVE_TIMEOUT_US;
        const int result = sceKernelReceiveMsgPipe(pipe, (void *)bufs[calls], sizeof(bufs[calls]), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &sizes[calls], &timeouts[calls]);
        calls++;
        if (result != 0) {
            first_failure = result;
            break;
        }
        zeros++;
    }
    PRINT("receives that returned 0", zeros);
    PRINT("calls made", calls);
    PRINT("the first failure's result, 0 if none", first_failure);

    // A blocking FULL send of 4 bytes for each pending receive
    int served = 0;
    for (int i = 0; i < zeros; i++) {
        size = 0xDEAD;
        timeout = TEST_WAIT_US;
        if (sceKernelSendMsgPipe(pipe, (void *)piece, sizeof(piece), MSG_PIPE_MODE_FULL, &size, &timeout) == 0)
            served++;
    }
    PRINT("blocking sends that returned 0", served);
    print_event("the pending receives", EVENT_OUT);

    TEST_WAIT_FOR(count_filled(piece, zeros) == zeros);
    PRINT("pending receives whose buffer got 4 bytes, as many as returned 0", count_filled(piece, zeros) == zeros);

    // One more DONT_WAIT receive, then a send that ends it
    PRINT("one more DONT_WAIT FULL receive of 4", sceKernelReceiveMsgPipe(pipe, last_buf, sizeof(last_buf), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &last_size, &last_timeout));
    size = 0xDEAD;
    timeout = TEST_WAIT_US;
    PRINT("the send that ends it", sceKernelSendMsgPipe(pipe, (void *)piece, sizeof(piece), MSG_PIPE_MODE_FULL, &size, &timeout));

    sceKernelDeleteMsgPipe(pipe);
    test_exit();
    return 0;
}
