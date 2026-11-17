// Where the head of a message pipe's receive queue goes when its own priority changes while it
// holds part of its bytes. The pipe has attr 0x2, which queues receivers by priority. Receivers A,
// B and C each wait for 8 bytes FULL on core 0, while this thread runs on core 1. The bytes are the
// letters "a" to "x" in order, so what each receiver got shows the order the bytes were handed out
// in. A waits first at priority 0x80 and gets "abcd" from a try send of 4 bytes, so it holds half
// of its bytes. Then B waits behind it at priority 0x88. Then A's priority changes, and once that
// returned C waits at priority 0x70, above A and B. Then one try send of the other 20 bytes goes to
// the receivers in queue order. Four cases with C, each on a new pipe:
// - A is set to the priority it has, 0x80.
// - A is lowered to 0x90, below B.
// - A is raised to 0x78, still above B.
// - A is set to the priority it has, without the first send, so A holds nothing and the 24 bytes
//   go in one send.
// C waiting last shows where a new receiver of the highest priority goes after the change.
// Three more cases have only A and B, and the try send of the other 12 bytes comes right after the
// priority change. They show the order the change itself leaves, with no later wait in between:
// - A is set to the priority it has, 0x80.
// - A is lowered to 0x90, below B.
// - A is raised to 0x78, still above B.
//
// On a PS Vita TV on firmware 3.65 A goes behind B in all three cases where it holds bytes, also
// when it keeps or raises its priority. With C, queued after the change, C goes right behind B, so
// B gets "efghijkl", C "mnopqrst" and A ends with "abcduvwx". Without C the send right after the
// change gives B "efghijkl" and A ends with "abcdmnop". Without the first send the receivers stay
// in priority order, so C gets "abcdefgh", A "ijklmnop" and B "qrstuvwx".
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define RECEIVERS 3
#define RECEIVE_SIZE 8
#define HEAD_BYTES 4
#define LONG_TIMEOUT (3 * TEST_WAIT_US)
#define NOT_DONE 0xDEAD

#define A_PRIORITY 0x80
#define B_PRIORITY 0x88
#define C_PRIORITY 0x70
#define LOWERED 0x90
#define RAISED 0x78

static const char *const names[RECEIVERS] = { "A", "B", "C" };

// Eight bytes for each receiver
static const char stream[] = "abcdefghijklmnopqrstuvwx";

static SceUID pipe;
static SceUID threads[RECEIVERS];
static volatile int result[RECEIVERS];
static SceSize moved[RECEIVERS];
static char received[RECEIVERS][16];

static int receiver(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt timeout = LONG_TIMEOUT;
    result[i] = sceKernelReceiveMsgPipe(pipe, received[i], RECEIVE_SIZE, MSG_PIPE_MODE_FULL, &moved[i], &timeout);
    return 0;
}

// Starts receiver i on core 0 and waits until it is in its wait
static void start(int i, int priority) {
    result[i] = NOT_DONE;
    moved[i] = 0;
    sceClibMemset(received[i], 0, sizeof(received[i]));
    threads[i] = sceKernelCreateThread("receiver", receiver, priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    TEST_WAIT_FOR(test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe));
}

static SceKernelMppInfo pipe_info(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info;
}

// Runs one case. A's priority is changed to new_priority after B waits. C only waits if with_c is set.
static void run(const char *name, int partial, int new_priority, int with_c) {
    const int count = with_c ? RECEIVERS : 2;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_RECEIVE_PRIO, CAPACITY, NULL);
    int first_send = NOT_DONE, a_waits = 0;
    SceSize first_size = NOT_DONE;
    size_t offset = 0;

    start(0, A_PRIORITY);
    if (partial) {
        first_send = sceKernelTrySendMsgPipe(pipe, (void *)stream, HEAD_BYTES, MSG_PIPE_MODE_FULL, &first_size);
        offset = HEAD_BYTES;
        // A waits for the other 4 bytes and no other thread runs, so this reads right after the send
        a_waits = test_in_wait(threads[0], WAIT_TYPE_MSG_PIPE, pipe);
    }
    start(1, B_PRIORITY);
    const int change = sceKernelChangeThreadPriority(threads[0], new_priority);
    // A and B still wait, the change moves no bytes
    const int a_waits_after = test_in_wait(threads[0], WAIT_TYPE_MSG_PIPE, pipe);
    if (with_c)
        start(2, C_PRIORITY);
    const int waiting = pipe_info().numReceiveWaitThreads;

    // Every receiver waits, so the bytes go to them in queue order and none stay in the pipe
    SceSize last_size = NOT_DONE;
    const int last_send = sceKernelTrySendMsgPipe(pipe, (void *)(stream + offset), count * RECEIVE_SIZE - offset, MSG_PIPE_MODE_FULL, &last_size);
    for (int i = 0; i < count; i++)
        test_join_thread(threads[i]);
    const SceKernelMppInfo info = pipe_info();
    sceKernelDeleteMsgPipe(pipe);

    if (partial) {
        PRINTF(first_send, "%s: the first send", name);
        PRINTF(first_size, "%s: the first send's size", name);
        PRINTF(a_waits, "%s: A still waits after the first send", name);
    }
    PRINTF(change, "%s: changing A's priority", name);
    PRINTF(a_waits_after, "%s: A still waits after the change", name);
    PRINTF(waiting, "%s: the receivers waiting before the last send", name);
    PRINTF(last_send, "%s: the last send", name);
    PRINTF(last_size, "%s: the last send's size", name);
    for (int i = 0; i < count; i++) {
        PRINTF(result[i], "%s: %s's result", name, names[i]);
        PRINTF(moved[i], "%s: %s got \"%s\", size", name, names[i], received[i]);
    }
    PRINTF(CAPACITY - info.freeSize, "%s: the bytes left in the pipe", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);

    run("A holds 4 of 8, set to its own priority", 1, A_PRIORITY, 1);
    run("A holds 4 of 8, lowered below B", 1, LOWERED, 1);
    run("A holds 4 of 8, raised", 1, RAISED, 1);
    run("A holds nothing, set to its own priority", 0, A_PRIORITY, 1);
    run("A holds 4 of 8, set to its own priority, no C", 1, A_PRIORITY, 0);
    run("A holds 4 of 8, lowered below B, no C", 1, LOWERED, 0);
    run("A holds 4 of 8, raised, no C", 1, RAISED, 0);

    test_exit();
    return 0;
}
