// A callback that notifies itself and then runs callbacks from inside its own run, so each run
// starts the next one a level deeper, until a chosen number of levels. The levels run inside a
// check or inside a delay with callbacks. Each level records its argument, its notification count
// and what the call inside it returned, and the main thread prints them once the outermost call
// returned. Each case uses a fresh callback. The depths stay well below the one where the kernel
// stack check would end the app.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define MAX_LEVELS 8
#define INNER_DELAY_US 1000

static SceUID cb;
static volatile int levels, use_delay, depth, deepest, runs;
static volatile int level_arg[MAX_LEVELS + 1], level_count[MAX_LEVELS + 1], level_inner[MAX_LEVELS + 1];

// Runs one level, and below the last level makes itself pending and runs the next one
static int nest_callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)common;
    const int level = ++depth;
    runs++;
    if (level > deepest)
        deepest = level;
    if (level <= MAX_LEVELS) {
        level_arg[level] = arg;
        level_count[level] = count;
    }
    // Where the call runs the callback after this run instead of inside it, the run count still
    // ends the chain
    if (level < levels && runs < levels) {
        // The next level gets its level number as the argument
        sceKernelNotifyCallback(cb, level + 1);
        const int inner = use_delay ? sceKernelDelayThreadCB(INNER_DELAY_US) : sceKernelCheckCallback();
        if (level <= MAX_LEVELS)
            level_inner[level] = inner;
    }
    depth--;
    return 0;
}

static void run(const char *name, int new_levels, int new_use_delay) {
    levels = new_levels;
    use_delay = new_use_delay;
    depth = deepest = runs = 0;
    for (int i = 0; i <= MAX_LEVELS; i++)
        level_arg[i] = level_count[i] = level_inner[i] = 0xDEAD;
    cb = sceKernelCreateCallback("nest", 0, nest_callback, NULL);
    const int created = test_created(cb);
    sceKernelNotifyCallback(cb, 1);
    const int outer = use_delay ? sceKernelDelayThreadCB(INNER_DELAY_US) : sceKernelCheckCallback();
    // What the outer call ran, kept before the next check can run the callback again
    const int outer_runs = runs, outer_deepest = deepest, outer_depth = depth;
    int arg_seen[MAX_LEVELS + 1], count_seen[MAX_LEVELS + 1], inner_seen[MAX_LEVELS + 1];
    for (int i = 0; i <= MAX_LEVELS; i++) {
        arg_seen[i] = level_arg[i];
        count_seen[i] = level_count[i];
        inner_seen[i] = level_inner[i];
    }
    // A run from here on doesn't nest
    levels = 0;
    const int left = sceKernelGetCallbackCount(cb);
    const int next = sceKernelCheckCallback();
    const int deleted = sceKernelDeleteCallback(cb);

    PRINTF(created, "%s: create the callback", name);
    PRINTF(outer, "%s: the outer call", name);
    PRINTF(outer_runs, "%s: runs", name);
    PRINTF(outer_deepest, "%s: the deepest level", name);
    PRINTF(outer_depth, "%s: levels still open after", name);
    for (int level = 1; level <= new_levels; level++) {
        PRINTF(arg_seen[level], "%s: level %d: the argument", name, level);
        PRINTF(count_seen[level], "%s: level %d: the count", name, level);
        if (level < new_levels)
            PRINTF(inner_seen[level], "%s: level %d: the call inside it", name, level);
    }
    PRINTF(left, "%s: notifications left", name);
    PRINTF(next, "%s: the next check", name);
    PRINTF(deleted, "%s: delete the callback", name);
}

int main(void) {
    run("3 levels by checks", 3, 0);
    run("4 levels by checks", 4, 0);
    run("3 levels by delays", 3, 1);
    run("4 levels by delays", 4, 1);
    // Deeper, still with room on the kernel stack: by the frame sizes the check inside level 9 is
    // the first that fails
    run("8 levels by checks", 8, 0);

    test_exit();
    return 0;
}
