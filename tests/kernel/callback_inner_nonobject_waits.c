// A callback that a non-object wait runs blocks in another non-object wait. The outer wait is a
// thread signal wait, a delay or a thread end wait with callbacks, and the wait inside the callback
// is one of the same three. Each case has its own thread, callback and gate. The callback holds at
// its end until the test lets it go, so the test makes its sends and reads at the points it picks.
// Every wait has a finite timeout.
// Cases:
// - a signal wait with callbacks and a signal wait inside: the first send ends the inner wait, and
//   a second send made while the callback holds stays pending for the outer wait to take
// - a signal wait with callbacks and a 500 ms delay inside: a send during the delay doesn't end it,
//   and the outer wait takes the signal once the callback is done
// - a delay with callbacks and a signal wait inside: after the callback the delay waits again with
//   its callback wait type, and it returns at its deadline
// - a thread end wait with callbacks and a thread end wait inside, on the same thread: the target
//   ends while the callback waits, and both waits get its status
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum OuterKind { OUTER_SIGNAL, OUTER_DELAY, OUTER_END };
enum InnerKind { INNER_SIGNAL, INNER_DELAY, INNER_END };

// The outer delay's length, and the inner delay's, which a send during it must not cut short
#define OUTER_DELAY_US 3000000
#define INNER_DELAY_US 500000

static SceUID cb, gate, target, target_gate;
static int outer_kind, inner_kind;
static volatile int runs, inner_done, inner_result, outer_result;
static volatile SceUInt64 inner_elapsed;
static int inner_status, outer_status;

// Waits in the inner wait the case picked, then holds until the test signals the gate
static int inner_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    runs++;
    SceUInt timeout = 2 * TEST_WAIT_US;
    const SceUInt64 start = sceKernelGetProcessTimeWide();
    switch (inner_kind) {
    case INNER_SIGNAL: inner_result = sceKernelWaitSignal(0, 0, &timeout); break;
    case INNER_DELAY: inner_result = sceKernelDelayThread(INNER_DELAY_US); break;
    default: inner_result = sceKernelWaitThreadEnd(target, &inner_status, &timeout); break;
    }
    inner_elapsed = sceKernelGetProcessTimeWide() - start;
    inner_done = 1;
    SceUInt hold = 2 * TEST_WAIT_US;
    sceKernelWaitSema(gate, 1, &hold);
    return 0;
}

// Waits in the outer wait the case picked, with the callback above registered
static int outer_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    cb = sceKernelCreateCallback("inner", 0, inner_callback, NULL);
    SceUInt timeout = 4 * TEST_WAIT_US;
    switch (outer_kind) {
    case OUTER_SIGNAL: outer_result = sceKernelWaitSignalCB(0, 0, &timeout); break;
    case OUTER_DELAY: outer_result = sceKernelDelayThreadCB(OUTER_DELAY_US); break;
    default: outer_result = sceKernelWaitThreadEndCB(target, &outer_status, &timeout); break;
    }
    sceKernelDeleteCallback(cb);
    return 0;
}

// Holds the end target of the thread end case until its gate is signaled, then ends with a status
static int target_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt gate_timeout = 2 * TEST_WAIT_US;
    sceKernelWaitSema(target_gate, 1, &gate_timeout);
    return 0x42;
}

static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : 0;
}

// Starts the outer thread of a case, with its callback's wait, and waits until it is in its wait
static SceUID start(int outer, int inner) {
    outer_kind = outer;
    inner_kind = inner;
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    runs = inner_done = 0;
    inner_result = outer_result = 0xDEAD;
    inner_status = outer_status = 0xDEAD;
    const SceUID thread = test_start_thread(outer_thread);
    TEST_WAIT_FOR(wait_type(thread) != 0);
    return thread;
}

// A signal wait with callbacks, and a signal wait inside the callback. The second send is made
// while the callback holds, so it is left pending for the outer wait.
static void signal_in_signal(void) {
    const SceUID thread = start(OUTER_SIGNAL, INNER_SIGNAL);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(wait_type(thread) == WAIT_TYPE_SIGNAL);
    PRINT("signal in signal: the first send", sceKernelSendSignal(thread));
    TEST_WAIT_FOR(inner_done);
    PRINT("signal in signal: the inner wait", inner_result);
    PRINT("signal in signal: the second send", sceKernelSendSignal(thread));
    sceKernelSignalSema(gate, 1);
    test_join_thread(thread);
    PRINT("signal in signal: the outer wait", outer_result);
    PRINT("signal in signal: runs of the callback", runs);
    sceKernelDeleteSema(gate);
}

// A signal wait with callbacks, and a delay inside the callback. A send during the delay doesn't
// end the delay, and the outer wait takes the signal once the callback is done.
static void signal_in_delay(void) {
    const SceUID thread = start(OUTER_SIGNAL, INNER_DELAY);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(wait_type(thread) == WAIT_TYPE_DELAY);
    PRINT("signal in delay: the send during the delay", sceKernelSendSignal(thread));
    TEST_WAIT_FOR(inner_done);
    PRINT("signal in delay: the delay's result", inner_result);
    PRINT("signal in delay: the delay lasted at least 250 ms", inner_elapsed >= 250000);
    sceKernelSignalSema(gate, 1);
    test_join_thread(thread);
    PRINT("signal in delay: the outer wait", outer_result);
    PRINT("signal in delay: runs of the callback", runs);
    sceKernelDeleteSema(gate);
}

// A delay with callbacks, and a signal wait inside the callback. Once the callback is done, the
// delay waits again with its callback wait type, up to its deadline.
static void delay_in_signal(void) {
    const SceUID thread = start(OUTER_DELAY, INNER_SIGNAL);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(wait_type(thread) == WAIT_TYPE_SIGNAL);
    PRINT("delay in signal: the send", sceKernelSendSignal(thread));
    TEST_WAIT_FOR(inner_done);
    PRINT("delay in signal: the signal wait's result", inner_result);
    sceKernelSignalSema(gate, 1);
    TEST_WAIT_FOR(wait_type(thread) == (WAIT_TYPE_DELAY | WAIT_TYPE_CB));
    PRINT("delay in signal: waits again with the callback wait type",
          wait_type(thread) == (WAIT_TYPE_DELAY | WAIT_TYPE_CB));
    test_join_thread(thread);
    PRINT("delay in signal: the outer delay's result", outer_result);
    PRINT("delay in signal: runs of the callback", runs);
    sceKernelDeleteSema(gate);
}

// A thread end wait with callbacks, and a thread end wait inside the callback on the same target.
// The target ends with 0x42 while the callback waits, and both waits get the status.
static void end_in_end(void) {
    target_gate = sceKernelCreateSema("target gate", 0, 0, 1, NULL);
    target = test_start_thread(target_thread);
    TEST_WAIT_FOR(wait_type(target) != 0);
    const SceUID thread = start(OUTER_END, INNER_END);
    sceKernelNotifyCallback(cb, 1);
    TEST_WAIT_FOR(wait_type(thread) == WAIT_TYPE_THREAD_END);
    sceKernelSignalSema(target_gate, 1);
    TEST_WAIT_FOR(inner_done);
    PRINT("end in end: the inner wait", inner_result);
    PRINT("end in end: the inner wait's status", inner_status);
    sceKernelSignalSema(gate, 1);
    test_join_thread(thread);
    PRINT("end in end: the outer wait", outer_result);
    PRINT("end in end: the outer wait's status", outer_status);
    PRINT("end in end: runs of the callback", runs);
    test_join_thread(target);
    sceKernelDeleteSema(target_gate);
    sceKernelDeleteSema(gate);
}

int main(void) {
    signal_in_signal();
    signal_in_delay();
    delay_in_signal();
    end_in_end();

    test_exit();
    return 0;
}
