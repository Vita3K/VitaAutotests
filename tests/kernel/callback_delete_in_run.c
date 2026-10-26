// What a callback's UID reports while the callback's run goes on after the callback was deleted,
// and what the checks and waits around that run return. A deleted callback may keep its UID until
// the run in progress ends, so the test reads the UID inside the run and again after it. Every case
// has fresh callbacks and fresh threads, and every wait has a timeout. The cases:
// - the callback deletes itself in a check, reads its UID, and deletes it again
// - the callback deletes itself in a semaphore wait with callbacks, which waits out its timeout
// - the callback makes itself pending and runs a nested check, the nested run deletes it, and the
//   outer run reads its UID after that check
// - a callback runs a nested check that runs another callback, which deletes the first one, and the
//   first one reads its UID after that check
// - another thread deletes a callback while it runs on core 0, blocked on a gate, and the main
//   thread on core 1 reads the UID during the run, then opens the gate and reads it again
// The calls on a UID are the count, the info, a notify, a cancel and a delete. Each case prints the
// results of those calls, of the checks and of the waits, and how often the callbacks ran.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define GATE_TIMEOUT_US 6000000

// What the calls on a callback's UID returned, in the order they were made
typedef struct UidState {
    int count, info, info_is_cb, notify, cancel;
} UidState;

// Makes the calls on cb that read or change it, except the delete
static void read_uid_state(SceUID cb, UidState *state) {
    CallbackInfo info = { .size = sizeof(info) };
    state->count = sceKernelGetCallbackCount(cb);
    state->info = sceKernelGetCallbackInfo(cb, (SceKernelCallbackInfo *)&info);
    state->info_is_cb = info.callbackId == cb;
    state->notify = sceKernelNotifyCallback(cb, 2);
    state->cancel = sceKernelCancelCallback(cb);
}

static void print_uid_state(const char *name, const UidState *state) {
    PRINTF(state->count, "%s: the notifications", name);
    PRINTF(state->info, "%s: the info", name);
    PRINTF(state->info_is_cb, "%s: the info is for the callback", name);
    PRINTF(state->notify, "%s: the notify", name);
    PRINTF(state->cancel, "%s: the cancel", name);
}

// A callback that deletes itself in a check, reads its UID, and deletes it again
static SceUID self_cb;
static volatile int self_runs, self_delete_inside, self_delete_again;
static UidState self_inside;

static int self_delete_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    self_runs++;
    self_delete_inside = sceKernelDeleteCallback(self_cb);
    read_uid_state(self_cb, &self_inside);
    self_delete_again = sceKernelDeleteCallback(self_cb);
    return 0;
}

static void run_self_delete_in_check(void) {
    self_runs = 0;
    self_delete_inside = self_delete_again = NOT_DONE;
    self_inside = (UidState){ NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE };
    self_cb = sceKernelCreateCallback("self delete", 0, self_delete_callback, NULL);
    sceKernelNotifyCallback(self_cb, 1);
    PRINT("self delete in a check: the check", sceKernelCheckCallback());
    PRINT("self delete in a check: the delete inside", self_delete_inside);
    print_uid_state("self delete in a check: inside", &self_inside);
    PRINT("self delete in a check: the second delete inside", self_delete_again);
    PRINT("self delete in a check: runs", self_runs);

    UidState after;
    read_uid_state(self_cb, &after);
    print_uid_state("self delete in a check: after", &after);
    PRINT("self delete in a check: the delete after", sceKernelDeleteCallback(self_cb));
    PRINT("self delete in a check: the next check", sceKernelCheckCallback());
    PRINT("self delete in a check: runs after the next check", self_runs);
}

// A callback that deletes itself in a semaphore wait with callbacks, in a thread that waits
static SceUID wait_sema, wait_cb;
static volatile int wait_runs, wait_delete_inside, wait_result;

static int wait_delete_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    wait_runs++;
    wait_delete_inside = sceKernelDeleteCallback(wait_cb);
    return 0;
}

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    wait_cb = sceKernelCreateCallback("wait delete", 0, wait_delete_callback, NULL);
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSemaCB(wait_sema, 1, &timeout);
    return 0;
}

static void run_self_delete_in_wait(void) {
    wait_runs = 0;
    wait_delete_inside = wait_result = NOT_DONE;
    wait_sema = sceKernelCreateSema("wait", 0, 0, 1, NULL);
    const SceUID thread = test_start_thread(wait_thread);
    TEST_WAIT_FOR(test_in_wait(thread, WAIT_TYPE_SEMA | WAIT_TYPE_CB, wait_sema));
    sceKernelNotifyCallback(wait_cb, 1);
    test_join_thread(thread);
    PRINT("self delete in a wait: the wait", wait_result);
    PRINT("self delete in a wait: the delete inside", wait_delete_inside);
    PRINT("self delete in a wait: runs", wait_runs);

    UidState after;
    read_uid_state(wait_cb, &after);
    print_uid_state("self delete in a wait: after", &after);
    PRINT("self delete in a wait: the delete after", sceKernelDeleteCallback(wait_cb));
    sceKernelDeleteSema(wait_sema);
}

// A callback that runs a nested check on its first run. The nested run is the same callback, which
// deletes it.
static SceUID nested_cb;
static volatile int nested_runs, nested_check_inside, nested_delete_inside;
static UidState nested_outer;

static int nested_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    nested_runs++;
    if (nested_runs == 1) {
        sceKernelNotifyCallback(nested_cb, 2);
        nested_check_inside = sceKernelCheckCallback();
        read_uid_state(nested_cb, &nested_outer);
    } else {
        nested_delete_inside = sceKernelDeleteCallback(nested_cb);
    }
    return 0;
}

static void run_nested_self(void) {
    nested_runs = 0;
    nested_check_inside = nested_delete_inside = NOT_DONE;
    nested_outer = (UidState){ NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE };
    nested_cb = sceKernelCreateCallback("nested", 0, nested_callback, NULL);
    sceKernelNotifyCallback(nested_cb, 1);
    PRINT("nested self delete: the check", sceKernelCheckCallback());
    PRINT("nested self delete: the nested check", nested_check_inside);
    PRINT("nested self delete: the delete in the nested run", nested_delete_inside);
    print_uid_state("nested self delete: in the outer run", &nested_outer);
    PRINT("nested self delete: runs", nested_runs);

    UidState after;
    read_uid_state(nested_cb, &after);
    print_uid_state("nested self delete: after both runs", &after);
    PRINT("nested self delete: the delete after both runs", sceKernelDeleteCallback(nested_cb));
    PRINT("nested self delete: the next check", sceKernelCheckCallback());
    PRINT("nested self delete: runs after the next check", nested_runs);
}

// Callback A notifies B and runs a nested check, which runs B, and B deletes A during that check
static SceUID chain_a, chain_b;
static volatile int chain_a_runs, chain_b_runs, chain_check_inside, chain_delete_inside;
static UidState chain_outer;

static int chain_a_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    chain_a_runs++;
    sceKernelNotifyCallback(chain_b, 1);
    chain_check_inside = sceKernelCheckCallback();
    read_uid_state(chain_a, &chain_outer);
    return 0;
}

static int chain_b_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    chain_b_runs++;
    chain_delete_inside = sceKernelDeleteCallback(chain_a);
    return 0;
}

static void run_nested_chain(void) {
    chain_a_runs = chain_b_runs = 0;
    chain_check_inside = chain_delete_inside = NOT_DONE;
    chain_outer = (UidState){ NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE, NOT_DONE };
    chain_a = sceKernelCreateCallback("chain a", 0, chain_a_callback, NULL);
    chain_b = sceKernelCreateCallback("chain b", 0, chain_b_callback, NULL);
    sceKernelNotifyCallback(chain_a, 1);
    PRINT("nested chain: the check", sceKernelCheckCallback());
    PRINT("nested chain: the nested check", chain_check_inside);
    PRINT("nested chain: the delete of A by B", chain_delete_inside);
    PRINT("nested chain: runs of A", chain_a_runs);
    PRINT("nested chain: runs of B", chain_b_runs);
    print_uid_state("nested chain: A in its run", &chain_outer);

    UidState after;
    read_uid_state(chain_a, &after);
    print_uid_state("nested chain: A after both runs", &after);
    PRINT("nested chain: the delete of A after both runs", sceKernelDeleteCallback(chain_a));
    sceKernelDeleteCallback(chain_b);
}

// A callback that waits on a gate on its first run only, so a later run can't block the owner
static SceUID depart_sema, depart_gate, depart_cb;
static volatile int depart_runs, depart_wait;

static int depart_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    (void)common;
    depart_runs++;
    if (depart_runs == 1) {
        SceUInt timeout = GATE_TIMEOUT_US;
        sceKernelWaitSema(depart_gate, 1, &timeout);
    }
    return 0;
}

// Creates the callback on core 0, then waits with callbacks on the semaphore
static int depart_owner(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    depart_cb = sceKernelCreateCallback("owner", 0, depart_callback, NULL);
    SceUInt timeout = 2 * TEST_WAIT_US;
    depart_wait = sceKernelWaitSemaCB(depart_sema, 1, &timeout);
    return 0;
}

// The owner's callback blocks on the gate while the main thread, on core 1, deletes it and reads
// its UID, then opens the gate and reads the UID again after the owner's wait ended
static void run_delete_on_other_core(void) {
    depart_runs = 0;
    depart_wait = NOT_DONE;
    depart_sema = sceKernelCreateSema("depart", 0, 0, 1, NULL);
    depart_gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    const SceUID owner = sceKernelCreateThread("owner", depart_owner,
                                               SCE_KERNEL_DEFAULT_PRIORITY_USER, 0x4000, 0,
                                               SCE_KERNEL_CPU_MASK_USER_0, NULL);
    sceKernelStartThread(owner, 0, NULL);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA | WAIT_TYPE_CB, depart_sema));
    sceKernelNotifyCallback(depart_cb, 1);
    TEST_WAIT_FOR(test_in_wait(owner, WAIT_TYPE_SEMA, depart_gate));

    PRINT("delete on another core: the delete during the run", sceKernelDeleteCallback(depart_cb));
    UidState during;
    read_uid_state(depart_cb, &during);
    print_uid_state("delete on another core: during the run", &during);

    sceKernelSignalSema(depart_gate, 1);
    test_join_thread(owner);
    PRINT("delete on another core: the owner's wait", depart_wait);
    PRINT("delete on another core: runs", depart_runs);

    UidState after;
    read_uid_state(depart_cb, &after);
    print_uid_state("delete on another core: after", &after);
    PRINT("delete on another core: the delete after", sceKernelDeleteCallback(depart_cb));
    sceKernelDeleteSema(depart_gate);
    sceKernelDeleteSema(depart_sema);
}

int main(void) {
    run_self_delete_in_check();
    run_self_delete_in_wait();
    run_nested_self();
    run_nested_chain();
    run_delete_on_other_core();

    test_exit();
    return 0;
}
