// Which outcome a thread signal wait gets when its timeout and a send both come before the waiter W
// can run again, what a send does to a thread that has already left its signal wait, and what W
// takes once it runs. W waits on core 0 below a gate thread that spins there without syscalls, so
// once W's wait ends it stays ready until the gate lets go, as in claim_races. The main thread runs
// on core 1. W's first step after the wait is held behind a semaphore that the main thread signals
// after its sends, so the sends always come before that step. Every case gets a fresh W and gate.
// The cases:
// - a signal wait with a 300 ms timeout that ends by the timeout, then two sends
// - a signal wait with a 1.5 s timeout, a send at once, and a second send after 2 s
// - a signal wait with a 3 s timeout and three sends
// - a signal wait with callbacks for 3 s, a notify, then a send, with the callback held until the
//   send is done
// The signal cases then take a signal with a zero timeout, once or twice. The controls are a delay
// of 300 ms that ends while W is ready, and a semaphore wait with a 1.5 s timeout that a signal
// ends at once. The cases print the statuses and wait types read while W is ready or held, whether
// the wait UID is -1, the send and notify results, W's result, whether its time left is 0 or above
// 1 s, and the results of the zero timeout takes.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/signal.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GATE_PRIORITY SCE_KERNEL_HIGHEST_PRIORITY_USER
#define W_PRIORITY 0xA0
#define SPINS 400000000
#define NOT_DONE 0xDEAD
// The longest a held step waits for the main thread, longer than the delays of any case
#define HOLD_US (2 * TEST_WAIT_US)

// The wait W starts in a case
typedef enum Kind { SIGNAL, SIGNAL_CB, DELAY, SEMA } Kind;

static Kind kind;
static SceUID gate, sema, go, woke, callback, w;
static volatile int gate_up, gate_open, done, callback_ready, callback_runs, n_takes;
static volatile int result, take[2];
static volatile SceUInt32 w_timeout, time_left;

static int gate_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    gate_up = 1;
    for (volatile int n = 0; n < SPINS && !gate_open; n++)
        ;
    return 0;
}

static void gate_close(void) {
    gate_up = gate_open = 0;
    gate = sceKernelCreateThread("gate", gate_entry, GATE_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(gate, 0, NULL);
    TEST_WAIT_FOR(gate_up);
}

static void gate_lift(void) {
    gate_open = 1;
    sceKernelWaitThreadEnd(gate, NULL, NULL);
    sceKernelDeleteThread(gate);
}

// Counts its run, then holds until the main thread has made its sends
static int callback_entry(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    callback_runs++;
    SceUInt32 hold = HOLD_US;
    sceKernelWaitSema(go, 1, &hold);
    return 0;
}

// Takes a pending signal with a zero timeout, n_takes times
static void take_signals(void) {
    SceUInt32 zero = 0;
    for (int i = 0; i < n_takes; i++)
        take[i] = sceKernelWaitSignal(0, 0, &zero);
}

static int w_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = w_timeout;
    SceUInt32 hold = HOLD_US;
    if (kind == DELAY) {
        result = sceKernelDelayThread(w_timeout);
    } else if (kind == SEMA) {
        result = sceKernelWaitSema(sema, 1, &timeout);
        time_left = timeout;
    } else if (kind == SIGNAL) {
        result = sceKernelWaitSignal(0, 0, &timeout);
        time_left = timeout;
        sceKernelSignalSema(woke, 1);
        sceKernelWaitSema(go, 1, &hold);
        take_signals();
    } else {
        callback = sceKernelCreateCallback("callback", 0, callback_entry, NULL);
        callback_ready = 1;
        result = sceKernelWaitSignalCB(0, 0, &timeout);
        time_left = timeout;
        take_signals();
        sceKernelDeleteCallback(callback);
    }
    done = 1;
    return 0;
}

// Starts W in a wait of kind k with timeout, and returns once W is in that wait
static void start_w(Kind k, SceUInt32 timeout, int takes) {
    kind = k;
    w_timeout = timeout;
    n_takes = takes;
    result = NOT_DONE;
    time_left = NOT_DONE;
    take[0] = NOT_DONE;
    take[1] = NOT_DONE;
    callback_ready = callback_runs = done = 0;
    go = sceKernelCreateSema("go", 0, 0, 1, NULL);
    woke = sceKernelCreateSema("woke", 0, 0, 1, NULL);
    w = sceKernelCreateThread("w", w_entry, W_PRIORITY, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(w, 0, NULL);
    test_wait_until_waiting(w);
}

static SceKernelThreadInfo info_of(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    sceKernelGetThreadInfo(thread, &info);
    return info;
}

// Lets the gate go, then waits for W to end, with a timeout, and deletes W and the case's objects
static void finish(void) {
    gate_lift();
    TEST_WAIT_FOR(done);
    SceUInt32 join = TEST_WAIT_US;
    sceKernelWaitThreadEnd(w, NULL, &join);
    sceKernelDeleteThread(w);
    sceKernelDeleteSema(go);
    sceKernelDeleteSema(woke);
}

// The timeout ends W's wait, then two sends reach it while it is ready
static void timeout_then_sends(void) {
    start_w(SIGNAL, 300000, 2);
    gate_close();
    sceKernelDelayThread(700000);
    const SceKernelThreadInfo info = info_of(w);
    const int send_first = sceKernelSendSignal(w);
    const int send_second = sceKernelSendSignal(w);
    sceKernelSignalSema(go, 1);
    finish();
    PRINT("timeout then sends: W's status after the timeout", info.status);
    PRINT("timeout then sends: W's wait type", info.waitType);
    PRINT("timeout then sends: W's wait UID is -1", info.waitId == -1);
    PRINT("timeout then sends: the first send", send_first);
    PRINT("timeout then sends: the second send", send_second);
    PRINT("timeout then sends: W's result", result);
    PRINT("timeout then sends: W's time left is 0", time_left == 0);
    PRINT("timeout then sends: W's first take", take[0]);
    PRINT("timeout then sends: W's second take", take[1]);
}

// A send ends the wait before the 1.5 s timeout, and a second send comes 2 s later
static void long_timeout_then_send(void) {
    start_w(SIGNAL, 1500000, 1);
    gate_close();
    const int send_first = sceKernelSendSignal(w);
    sceKernelDelayThread(2000000);
    const int status = info_of(w).status;
    const int send_second = sceKernelSendSignal(w);
    sceKernelSignalSema(go, 1);
    finish();
    PRINT("long timeout: the first send", send_first);
    PRINT("long timeout: W's status after 2 s", status);
    PRINT("long timeout: the second send", send_second);
    PRINT("long timeout: W's result", result);
    PRINT("long timeout: W's time left is above 1 s", time_left > 1000000);
    PRINT("long timeout: W's take", take[0]);
}

// Three sends reach W while its wait is still in progress
static void three_sends(void) {
    start_w(SIGNAL, 3000000, 2);
    gate_close();
    const int send_first = sceKernelSendSignal(w);
    // W records that its wait returned, which takes the first signal. The gate keeps W from
    // getting there until it lifts, so this ends at its bound, and the next sends then reach W
    // while it is ready and has not run.
    SceUInt32 woke_timeout = TEST_WAIT_US;
    sceKernelWaitSema(woke, 1, &woke_timeout);
    const int send_second = sceKernelSendSignal(w);
    const int send_third = sceKernelSendSignal(w);
    sceKernelSignalSema(go, 1);
    finish();
    PRINT("three sends: the first send", send_first);
    PRINT("three sends: the second send", send_second);
    PRINT("three sends: the third send", send_third);
    PRINT("three sends: W's result", result);
    PRINT("three sends: W's first take", take[0]);
    PRINT("three sends: W's second take", take[1]);
}

// A notify ends the wait with callbacks, then a send reaches W before its callback has run
static void callback_notify_then_send(void) {
    start_w(SIGNAL_CB, 3000000, 1);
    gate_close();
    const int notify = sceKernelNotifyCallback(callback, 1);
    const int still_waiting = info_of(w).waitType == (WAIT_TYPE_SIGNAL | WAIT_TYPE_CB);
    const int send = sceKernelSendSignal(w);
    sceKernelSignalSema(go, 1);
    finish();
    PRINT("with callbacks, notify then send: the notify", notify);
    PRINT("with callbacks, notify then send: W still in its signal wait after the notify", still_waiting);
    PRINT("with callbacks, notify then send: the send", send);
    PRINT("with callbacks, notify then send: W's result", result);
    PRINT("with callbacks, notify then send: runs of the callback", callback_runs);
    PRINT("with callbacks, notify then send: W's time left is above 1 s", time_left > 1000000);
    PRINT("with callbacks, notify then send: W's take", take[0]);
}

// A delay that ends while W is ready, to compare with the signal wait
static void delay_control(void) {
    start_w(DELAY, 300000, 0);
    gate_close();
    sceKernelDelayThread(700000);
    const SceKernelThreadInfo info = info_of(w);
    finish();
    PRINT("delay control: W's status after the delay", info.status);
    PRINT("delay control: W's wait type", info.waitType);
    PRINT("delay control: W's wait UID is -1", info.waitId == -1);
    PRINT("delay control: W's result", result);
}

// A signal at once ends a semaphore wait with a 1.5 s timeout, to compare with the signal cases
static void sema_control(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    start_w(SEMA, 1500000, 0);
    gate_close();
    const int signal = sceKernelSignalSema(sema, 1);
    sceKernelDelayThread(2000000);
    finish();
    sceKernelDeleteSema(sema);
    PRINT("semaphore control: the signal", signal);
    PRINT("semaphore control: W's result", result);
    PRINT("semaphore control: W's time left is above 1 s", time_left > 1000000);
}

int main(void) {
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    // The main thread only moves to core 1 when it is next scheduled
    sceKernelDelayThread(1000);

    timeout_then_sends();
    long_timeout_then_send();
    three_sends();
    callback_notify_then_send();
    delay_control();
    sema_control();

    test_exit();
    return 0;
}
