// The order one pass over a thread's callbacks runs them in, when the callbacks change while the pass
// runs: a later callback notified by an earlier one, an earlier one notified by a later one, a
// callback created and notified inside a run, and a callback cancelled by an earlier run. Everything
// runs on the main thread with no wait, so nothing races. Cases:
// - five callbacks created in order, the second one deleted, a sixth created, then the live ones
//   notified in reverse creation order
// - an earlier callback notifies a later one when it runs, which runs in the same check
// - a later callback notifies an earlier one when it runs, which runs in the next check
// - a callback creates another one inside its run and notifies it, which runs in the same check
// - an earlier callback cancels a later one when it runs, which then doesn't run
// Each case prints the results of the checks and of the calls made inside the runs, the labels of
// the callbacks in the order they ran, how often each one ran, and its notifications left after.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define NOT_DONE 0xDEAD
#define MAX_ORDER 8

// What a callback does when a pass runs it
typedef enum Op {
    OP_NONE,
    OP_NOTIFY,
    OP_CANCEL,
    OP_CREATE_NOTIFY
} Op;

// A callback's label, how often it ran, what it does on a run to the callback in target, and the
// result of that call
typedef struct Step {
    int label;
    int runs;
    Op op;
    SceUID target;
    int result;
} Step;

// The labels of the callbacks, in the order the passes ran them
static int order[MAX_ORDER], order_count;
// The callback that OP_CREATE_NOTIFY creates, and the step it runs
static SceUID made;
static Step made_step;

static int step_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    Step *step = common;
    if (order_count < MAX_ORDER)
        order[order_count++] = step->label;
    step->runs++;
    switch (step->op) {
    case OP_NONE: break;
    case OP_NOTIFY: step->result = sceKernelNotifyCallback(step->target, 1); break;
    case OP_CANCEL: step->result = sceKernelCancelCallback(step->target); break;
    case OP_CREATE_NOTIFY:
        made = sceKernelCreateCallback("made", 0, step_callback, &made_step);
        step->result = sceKernelNotifyCallback(made, 1);
        break;
    }
    return 0;
}

// Prints how many callbacks ran and the label of each one, in the order they ran
static void print_order(const char *name) {
    PRINTF(order_count, "%s: callbacks that ran", name);
    for (int i = 0; i < order_count; i++)
        PRINTF(order[i], "%s: run %d is the callback labelled", name, i + 1);
}

// Five callbacks created in order, the second one deleted, a sixth created, then the live ones
// notified in reverse creation order
static void creation_order(void) {
    const char *name = "creation order";
    Step steps[6] = { { 0 } };
    SceUID cb[6];
    order_count = 0;
    for (int i = 0; i < 5; i++) {
        steps[i].label = i + 1;
        cb[i] = sceKernelCreateCallback("order", 0, step_callback, &steps[i]);
    }
    PRINT("creation order: delete the second", sceKernelDeleteCallback(cb[1]));
    steps[5].label = 6;
    cb[5] = sceKernelCreateCallback("order", 0, step_callback, &steps[5]);

    // The live callbacks, in creation order
    const int live[] = { 0, 2, 3, 4, 5 };
    for (int i = 4; i >= 0; i--)
        PRINTF(sceKernelNotifyCallback(cb[live[i]], 1), "%s: notify the callback labelled %d", name,
               steps[live[i]].label);
    PRINT("creation order: the check", sceKernelCheckCallback());
    print_order(name);
    for (int i = 1; i < 5; i++)
        PRINTF(cb[live[i]] > cb[live[i - 1]], "%s: UID of label %d is above the UID of label %d", name,
               steps[live[i]].label, steps[live[i - 1]].label);
    for (int i = 0; i < 5; i++)
        PRINTF(steps[live[i]].runs, "%s: runs of the callback labelled %d", name, steps[live[i]].label);
    for (int i = 0; i < 5; i++)
        sceKernelDeleteCallback(cb[live[i]]);
}

// The earlier callback notifies the later one when it runs, so the later one runs in the same check
static void later_notified_by_earlier(void) {
    const char *name = "later notified by earlier";
    Step first = { .label = 1, .op = OP_NOTIFY }, second = { .label = 2 };
    order_count = 0;
    const SceUID cb_first = sceKernelCreateCallback("first", 0, step_callback, &first);
    const SceUID cb_second = sceKernelCreateCallback("second", 0, step_callback, &second);
    first.target = cb_second;
    PRINTF(sceKernelNotifyCallback(cb_first, 1), "%s: notify the first", name);
    PRINTF(sceKernelCheckCallback(), "%s: the check", name);
    print_order(name);
    PRINTF(first.result, "%s: the first's notify of the second", name);
    PRINTF(first.runs, "%s: runs of the first", name);
    PRINTF(second.runs, "%s: runs of the second", name);
    PRINTF(sceKernelGetCallbackCount(cb_second), "%s: the second's notifications after", name);
    sceKernelDeleteCallback(cb_first);
    sceKernelDeleteCallback(cb_second);
}

// The later callback notifies the earlier one when it runs, so the earlier one runs in the next check
static void earlier_notified_by_later(void) {
    const char *name = "earlier notified by later";
    Step earlier = { .label = 1 }, later = { .label = 2, .op = OP_NOTIFY };
    order_count = 0;
    const SceUID cb_earlier = sceKernelCreateCallback("earlier", 0, step_callback, &earlier);
    const SceUID cb_later = sceKernelCreateCallback("later", 0, step_callback, &later);
    later.target = cb_earlier;
    PRINTF(sceKernelNotifyCallback(cb_later, 1), "%s: notify the later", name);
    PRINTF(sceKernelCheckCallback(), "%s: the first check", name);
    PRINTF(earlier.runs, "%s: runs of the earlier after the first check", name);
    PRINTF(sceKernelGetCallbackCount(cb_earlier), "%s: the earlier's notifications after the first check",
           name);
    PRINTF(sceKernelCheckCallback(), "%s: the second check", name);
    PRINTF(earlier.runs, "%s: runs of the earlier after the second check", name);
    PRINTF(sceKernelGetCallbackCount(cb_earlier), "%s: the earlier's notifications after the second check",
           name);
    print_order(name);
    PRINTF(later.result, "%s: the later's notify of the earlier", name);
    sceKernelDeleteCallback(cb_earlier);
    sceKernelDeleteCallback(cb_later);
}

// A callback that creates another one inside its run and notifies it, so the new one runs in the
// same check
static void created_inside_run(void) {
    const char *name = "created inside a run";
    Step creator = { .label = 1, .op = OP_CREATE_NOTIFY };
    made_step.label = 9;
    made_step.runs = 0;
    made = -1;
    order_count = 0;
    const SceUID cb_creator = sceKernelCreateCallback("creator", 0, step_callback, &creator);
    PRINTF(sceKernelNotifyCallback(cb_creator, 1), "%s: notify the creator", name);
    PRINTF(sceKernelCheckCallback(), "%s: the check", name);
    print_order(name);
    PRINTF(creator.result, "%s: the creator's notify of the new callback", name);
    PRINTF(made_step.runs, "%s: runs of the new callback", name);
    PRINTF(sceKernelCheckCallback(), "%s: the next check", name);
    PRINTF(made_step.runs, "%s: runs of the new callback after the next check", name);
    sceKernelDeleteCallback(cb_creator);
    sceKernelDeleteCallback(made);
}

// The earlier callback cancels the later one when it runs, so the later one doesn't run
static void cancelled_by_earlier(void) {
    const char *name = "cancelled by earlier";
    Step canceller = { .label = 1, .op = OP_CANCEL }, victim = { .label = 2 };
    order_count = 0;
    const SceUID cb_canceller = sceKernelCreateCallback("canceller", 0, step_callback, &canceller);
    const SceUID cb_victim = sceKernelCreateCallback("victim", 0, step_callback, &victim);
    canceller.target = cb_victim;
    PRINTF(sceKernelNotifyCallback(cb_victim, 1), "%s: notify the victim", name);
    PRINTF(sceKernelNotifyCallback(cb_canceller, 1), "%s: notify the canceller", name);
    PRINTF(sceKernelCheckCallback(), "%s: the check", name);
    print_order(name);
    PRINTF(canceller.result, "%s: the canceller's cancel of the victim", name);
    PRINTF(victim.runs, "%s: runs of the victim", name);
    PRINTF(sceKernelGetCallbackCount(cb_victim), "%s: the victim's notifications after", name);
    PRINTF(sceKernelCheckCallback(), "%s: the next check", name);
    PRINTF(victim.runs, "%s: runs of the victim after the next check", name);
    sceKernelDeleteCallback(cb_canceller);
    sceKernelDeleteCallback(cb_victim);
}

int main(void) {
    creation_order();
    later_notified_by_earlier();
    earlier_notified_by_later();
    created_inside_run();
    cancelled_by_earlier();

    test_exit();
    return 0;
}
