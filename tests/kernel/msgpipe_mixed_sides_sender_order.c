// In which order a message pipe that queues senders by priority lets two waiting senders in
// when a receive with DONT_REMOVE that waited at the head of the queue leaves by its timeout.
// Senders and receivers can only wait on a pipe at the same time when a receive with DONT_REMOVE
// waits, so one case is built around two of them. The pipe has 4096 bytes and attr 0x1,
// so senders queue by priority and receivers in arrival order. A lower number is a higher
// priority. The steps are:
// 1. The pipe gets 3000 held bytes of 'a' from a try send.
// 2. Receives FULL of 4096 with DONT_REMOVE wait. In the two peeks case R1 waits first and R2
//    behind it. The reference case has neither. R2 has
//    priority 190 and gives up after 300 ms, S1 and S2 after 1.5 s, R1 after 4 s.
// 3. S1, of priority 100, sends 2000 bytes of 'b' FULL. In the two peeks case this is meant to
//    show R1 ending with 4096 bytes, R2 still waiting, and S1 waiting with only 1096 bytes of
//    room. The console may complete R2 here too.
// 4. S2, of priority 150, sends 2000 bytes of 'c' FULL and is meant to wait too.
// 5. R2's timeout passes. The test then waits up to 200 ms for S1 and S2 to stop waiting.
// 6. Two probe receives let the senders in one at a time. Each is a blocking receive ASAP that
//    waits 10 ms, of as many bytes as leave exactly 2000 bytes of room, or of 1 byte if the pipe
//    already has that much room. After each the test records which sender stopped waiting.
// 7. Blocking receives ASAP of 4096 that wait 10 ms take what the pipe still holds, until one
//    fails or after 5 of them.
// The question is which sender ends first, S1 or S2, read from which sender stops waiting at step
// 5 and after each probe. The bytes the receives get are printed too, but they don't settle the
// order, since after a send serves a DONT_REMOVE receive the console hands out bytes out of step
// (see msgpipe_peek_stale). The reference case has S1 and S2 queue only behind each other. After
// each step the test records the pipe's free size, its waiting senders and receivers, and which
// threads wait on the pipe, and prints them only once every thread was joined. The pipe info
// reports all its waiters as the one side it last recorded, so with both sides waiting the
// waiting senders can count receivers too, and the waiting receivers can read 0. Then it prints
// each call's result and size, and for each call's buffer, over the size the call asked for, the
// first and last 4 bytes, the count of each letter and the runs of letters. Receive buffers start
// filled with their own marker, 'x' for R1, 'y' for R2 and 'w' for the probes and drains, so bytes
// a receive didn't get show, and the senders' buffers are printed too. Each case ends by
// canceling and deleting the pipe. Every thread runs on core 0 and the main thread on core 1.
//
// On a PS Vita TV on firmware 3.65 the send at step 3 completes both DONT_REMOVE receives. R1 and
// R2 each end with 3000 'a' and 1096 'b', and only S1 waits, so no receive waits next to the
// senders. S2 queues behind S1, and the probes let S1 in first, then S2, the same order as in the
// reference case. The probes get 'b' bytes where the reference case gets 'a', so the bytes are out
// of step after the send, like in msgpipe_peek_stale.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define PEEK_TIMEOUT_US 300000
#define SEND_TIMEOUT_US 1500000
#define LONG_TIMEOUT_US 4000000
// How long a probe or drain receive waits, nothing else can end it
#define IDLE_TIMEOUT_US 10000
#define SETTLE_POLLS 200
#define SLOTS 4
#define STEPS 9
#define PROBES 2
#define DRAINS 5
#define SEND_SIZE 2000
#define MSG_PIPE_ATTR_SEND_PRIO 0x1
#define PEEK_PRIORITY 190
#define S1_PRIORITY 100
#define S2_PRIORITY 150

enum Slot {
    R1,
    R2,
    S1,
    S2,
};

static const char *const names[SLOTS] = { "R1", "R2", "S1", "S2" };
static const char markers[SLOTS] = { 'x', 'y', 'b', 'c' };
static const char letters[] = "abcwxy";

typedef struct Op {
    int send;
    SceSize size;
    SceUInt32 mode;
    SceUInt32 timeout;
    int priority;
} Op;

typedef struct Snapshot {
    SceKernelMppInfo info;
    // 1 if the thread waits on the pipe, 0 if not, -1 if it was not started
    int waits[SLOTS];
} Snapshot;

typedef struct Receive {
    SceSize asked;
    int result;
    SceSize size;
    char bytes[CAPACITY];
} Receive;

static SceUID pipe;
static Op ops[SLOTS];
static SceUID threads[SLOTS];
static volatile int result[SLOTS], done[SLOTS];
static SceSize moved[SLOTS];
static char bufs[SLOTS][CAPACITY];
static Snapshot snapshots[STEPS];
static Receive probes[PROBES], drains[DRAINS];
static char initial[3000];

static int worker(SceSize args, void *argp) {
    (void)args;
    const int i = *(const int *)argp;
    SceUInt32 timeout = ops[i].timeout;
    if (ops[i].send)
        result[i] = sceKernelSendMsgPipe(pipe, bufs[i], ops[i].size, ops[i].mode, &moved[i], &timeout);
    else
        result[i] = sceKernelReceiveMsgPipe(pipe, bufs[i], ops[i].size, ops[i].mode, &moved[i], &timeout);
    __atomic_store_n(&done[i], 1, __ATOMIC_SEQ_CST);
    return 0;
}

// Starts slot i on core 0 and returns once that thread waits or its call returned
static void start(int i, Op op) {
    ops[i] = op;
    result[i] = NOT_WRITTEN;
    done[i] = 0;
    moved[i] = NOT_WRITTEN;
    sceClibMemset(bufs[i], markers[i], CAPACITY);
    threads[i] = sceKernelCreateThread(names[i], worker, op.priority, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(threads[i], sizeof(i), &i);
    for (int t = 0; t < 500 && !done[i]; t++) {
        SceKernelThreadInfo info = { .size = sizeof(info) };
        if (sceKernelGetThreadInfo(threads[i], &info) < 0 || info.waitType != 0)
            return;
        sceKernelDelayThread(1000);
    }
}

// Records the pipe's info and which started threads wait on the pipe
static void snapshot(int step) {
    snapshots[step].info = (SceKernelMppInfo){ .size = sizeof(SceKernelMppInfo) };
    sceKernelGetMsgPipeInfo(pipe, &snapshots[step].info);
    for (int i = 0; i < SLOTS; i++)
        snapshots[step].waits[i] = threads[i] > 0 ? test_in_wait(threads[i], WAIT_TYPE_MSG_PIPE, pipe) : -1;
}

static void print_snapshot(const char *name, int step, const char *what) {
    const Snapshot *s = &snapshots[step];
    PRINTF(s->info.freeSize, "%s: %s: the free size", name, what);
    PRINTF(s->info.numSendWaitThreads, "%s: %s: the waiting senders", name, what);
    PRINTF(s->info.numReceiveWaitThreads, "%s: %s: the waiting receivers", name, what);
    for (int i = 0; i < SLOTS; i++)
        if (s->waits[i] >= 0)
            PRINTF(s->waits[i], "%s: %s: %s waits on the pipe", name, what, names[i]);
}

static char letter(char c) {
    for (const char *l = letters; *l; l++)
        if (c == *l)
            return c;
    return '?';
}

// Prints the first and last 4 bytes, the count of each letter and the runs of letters of size
// bytes, other bytes shown as '?'
static void print_bytes(const char *what, const char *bytes, SceSize size) {
    if (size > CAPACITY)
        size = 0;
    char first[5] = { 0 }, last[5] = { 0 }, runs[48] = { 0 }, counts_text[80] = { 0 };
    int counts[sizeof(letters) - 1] = { 0 }, other = 0, run_count = 0, used = 0;
    for (SceSize k = 0; k < size; k++) {
        const char c = letter(bytes[k]);
        if (k < 4)
            first[k] = c;
        if (size >= 4 && k + 4 >= size)
            last[k + 4 - size] = c;
        if (c == '?')
            other++;
        for (int l = 0; letters[l]; l++)
            if (c == letters[l])
                counts[l]++;
        if (k > 0 && c == letter(bytes[k - 1]))
            continue;
        SceSize length = 1;
        while (k + length < size && letter(bytes[k + length]) == c)
            length++;
        if (run_count < 4)
            used += sceClibSnprintf(runs + used, sizeof(runs) - used, "%s%c%u", run_count ? " " : "", c, (unsigned)length);
        else if (run_count == 4)
            used += sceClibSnprintf(runs + used, sizeof(runs) - used, " +");
        run_count++;
    }
    int counts_used = 0;
    for (int l = 0; letters[l]; l++)
        counts_used += sceClibSnprintf(counts_text + counts_used, sizeof(counts_text) - counts_used, "%c %d, ", letters[l], counts[l]);
    PRINTF(size, "%s: first \"%s\", last \"%s\", size", what, first, last);
    PRINTF(run_count, "%s: %sother %d, runs \"%s\", run count", what, counts_text, other, runs);
}

// Runs a blocking receive ASAP of asked bytes that waits 10 ms into r
static void receive(Receive *r, SceSize asked) {
    SceUInt32 timeout = IDLE_TIMEOUT_US;
    r->asked = asked;
    r->size = NOT_WRITTEN;
    sceClibMemset(r->bytes, 'w', sizeof(r->bytes));
    r->result = sceKernelReceiveMsgPipe(pipe, r->bytes, asked, MSG_PIPE_MODE_ASAP, &r->size, &timeout);
}

// Returns the size of a receive that leaves exactly SEND_SIZE bytes of room, or 1 if the pipe has
// that much room already
static SceSize probe_size(void) {
    SceKernelMppInfo info = { .size = sizeof(info) };
    sceKernelGetMsgPipeInfo(pipe, &info);
    return info.freeSize < SEND_SIZE ? SEND_SIZE - info.freeSize : 1;
}

static void print_receive(const char *name, const char *what, const Receive *r) {
    char line[64];
    PRINTF(r->asked, "%s: %s, its asked size", name, what);
    PRINTF(r->result, "%s: %s", name, what);
    PRINTF(r->size, "%s: %s, its size", name, what);
    sceClibSnprintf(line, sizeof(line), "%s: %s got", name, what);
    if (r->result >= 0)
        print_bytes(line, r->bytes, r->size);
}

// Runs one case with peeks receives FULL with DONT_REMOVE and held bytes of 'a' in the pipe
static void run(const char *name, int peeks, SceSize held) {
    char what[64];
    for (int i = 0; i < SLOTS; i++)
        threads[i] = 0;
    pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, MSG_PIPE_ATTR_SEND_PRIO, CAPACITY, NULL);
    SceSize initial_size = NOT_WRITTEN;
    const int initial_result = sceKernelTrySendMsgPipe(pipe, initial, held, MSG_PIPE_MODE_FULL, &initial_size);
    snapshot(0);

    if (peeks == 2)
        start(R1, (Op){ 0, CAPACITY, MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, LONG_TIMEOUT_US, SCE_KERNEL_DEFAULT_PRIORITY_USER });
    if (peeks > 0)
        start(R2, (Op){ 0, CAPACITY, MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_REMOVE, PEEK_TIMEOUT_US, PEEK_PRIORITY });
    snapshot(1);

    start(S1, (Op){ 1, SEND_SIZE, MSG_PIPE_MODE_FULL, SEND_TIMEOUT_US, S1_PRIORITY });
    snapshot(2);
    start(S2, (Op){ 1, SEND_SIZE, MSG_PIPE_MODE_FULL, SEND_TIMEOUT_US, S2_PRIORITY });
    snapshot(3);

    if (peeks > 0) {
        TEST_WAIT_FOR(done[R2]);
        for (int t = 0; t < SETTLE_POLLS; t++) {
            if (!test_in_wait(threads[S1], WAIT_TYPE_MSG_PIPE, pipe) && !test_in_wait(threads[S2], WAIT_TYPE_MSG_PIPE, pipe))
                break;
            sceKernelDelayThread(1000);
        }
    }
    snapshot(4);

    for (int k = 0; k < PROBES; k++) {
        receive(&probes[k], probe_size());
        snapshot(5 + k);
    }

    int drain_count = 0;
    while (drain_count < DRAINS) {
        Receive *d = &drains[drain_count++];
        receive(d, CAPACITY);
        if (d->result < 0)
            break;
    }
    snapshot(7);

    for (int i = 0; i < SLOTS; i++)
        if (threads[i] > 0)
            TEST_WAIT_FOR(done[i]);
    snapshot(8);
    for (int i = 0; i < SLOTS; i++)
        if (threads[i] > 0)
            test_join_thread(threads[i]);
    const int cancel_result = sceKernelCancelMsgPipe(pipe, NULL, NULL);
    const int delete_result = sceKernelDeleteMsgPipe(pipe);

    PRINTF(initial_result, "%s: 1, try send of %u 'a'", name, (unsigned)held);
    PRINTF(initial_size, "%s: 1, try send of %u 'a', its size", name, (unsigned)held);
    print_snapshot(name, 0, "1, after the held bytes");
    if (peeks > 0)
        print_snapshot(name, 1, "2, the peeks wait");
    print_snapshot(name, 2, "3, after S1 sends 2000 'b'");
    print_snapshot(name, 3, "4, after S2 sends 2000 'c'");
    if (peeks > 0)
        print_snapshot(name, 4, "5, after R2's timeout");
    for (int k = 0; k < PROBES; k++) {
        sceClibSnprintf(what, sizeof(what), "6, probe %d", k + 1);
        print_receive(name, what, &probes[k]);
        sceClibSnprintf(what, sizeof(what), "6, after probe %d", k + 1);
        print_snapshot(name, 5 + k, what);
    }
    for (int k = 0; k < drain_count; k++) {
        sceClibSnprintf(what, sizeof(what), "7, drain %d", k + 1);
        print_receive(name, what, &drains[k]);
    }
    print_snapshot(name, 7, "7, after the drains");
    print_snapshot(name, 8, "every call ended");
    for (int i = 0; i < SLOTS; i++) {
        if (threads[i] <= 0)
            continue;
        PRINTF(result[i], "%s: %s's result", name, names[i]);
        PRINTF(moved[i], "%s: %s's size", name, names[i]);
        sceClibSnprintf(what, sizeof(what), "%s: %s's buffer", name, names[i]);
        print_bytes(what, bufs[i], ops[i].size);
    }
    PRINTF(cancel_result, "%s: cancel", name);
    PRINTF(delete_result, "%s: delete", name);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    sceClibMemset(initial, 'a', sizeof(initial));

    run("two peeks", 2, 3000);
    run("reference", 0, 3000);

    test_exit();
    return 0;
}
