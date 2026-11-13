// What sceKernelGetThreadInfo reports for a thread that is blocked in a display vblank wait. A
// helper thread B blocks in the wait under test. The main thread polls B's thread info until the
// status is waiting and keeps that one snapshot. A vblank comes every 16 ms, so B can end its wait
// before the poll sees it. The case is then repeated with a fresh B until a snapshot is caught. Only
// the caught snapshot is printed. It is printed after B ended. For each case it prints the status,
// the wait type, how the wait id compares to -1 and 0, the class sceKernelGetThreadmgrUIDClass
// returns for it and the result of sceKernelGetEventInfo on it. The cases:
// - sceDisplayWaitVblankStart
// - sceDisplayWaitVblankStartCB
// - sceDisplayWaitVblankStartMulti with 4
// - sceDisplayWaitVblankStartMultiCB with 4
// - two threads: two Bs wait in sceDisplayWaitVblankStartMulti with 4 at the same time, and both
//   snapshots come from the same poll
// After the cases it prints whether the wait id is the same between the plain and the CB wait,
// between the Multi and the MultiCB wait, between the plain and the Multi wait, and between the two
// threads. sceDisplayWaitSetFrameBuf and sceDisplayWaitSetFrameBufCB are left out since they need
// a framebuffer set first, and the other display tests cover them.
//
// On a PS Vita TV on firmware 3.65 every case reports a waiting thread with the event wait type,
// 0x10000, and with the callback bit for the CB waits. The wait id is positive and the same in every
// case and for both threads. It isn't a UID of the app: its class lookup gives 0x80024501 and
// sceKernelGetEventInfo gives 0x80028201.
#include "vblank_cb.h"

enum Wait { VBLANK, VBLANK_CB, MULTI, MULTI_CB };

#define VBLANKS 4
#define MAX_TRIES 500

static volatile int wait_kind;
static volatile int done;

static int b_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    switch (wait_kind) {
    case VBLANK:
        sceDisplayWaitVblankStart();
        break;
    case VBLANK_CB:
        sceDisplayWaitVblankStartCB();
        break;
    case MULTI:
        sceDisplayWaitVblankStartMulti(VBLANKS);
        break;
    default:
        sceDisplayWaitVblankStartMultiCB(VBLANKS);
        break;
    }
    done++;
    return 0;
}

static int waiting(SceUID thread, SceKernelThreadInfo *info) {
    info->size = sizeof(*info);
    return sceKernelGetThreadInfo(thread, info) == 0 && (info->status & THREAD_STATUS_WAITING) != 0;
}

// Catches a snapshot of a thread in the wait, with a fresh thread for each try
static int catch_one(int kind, SceKernelThreadInfo *info) {
    for (int tries = 0; tries < MAX_TRIES; tries++) {
        wait_kind = kind;
        done = 0;
        const SceUID b = start_w(b_entry);
        int caught = 0;
        while (!done && !caught) {
            caught = waiting(b, info);
            sceKernelDelayThread(100);
        }
        TEST_WAIT_FOR(done);
        test_join_thread(b);
        if (caught)
            return 1;
    }
    return 0;
}

// Catches snapshots of two threads in the wait at the same poll
static int catch_two(SceKernelThreadInfo *info1, SceKernelThreadInfo *info2) {
    for (int tries = 0; tries < MAX_TRIES; tries++) {
        wait_kind = MULTI;
        done = 0;
        const SceUID b1 = start_w(b_entry);
        const SceUID b2 = start_w(b_entry);
        int caught = 0;
        while (done < 2 && !caught) {
            caught = waiting(b1, info1) && waiting(b2, info2);
            sceKernelDelayThread(100);
        }
        TEST_WAIT_FOR(done == 2);
        test_join_thread(b1);
        test_join_thread(b2);
        if (caught)
            return 1;
    }
    return 0;
}

static void print_info(const char *name, int caught, const SceKernelThreadInfo *info) {
    PRINTF(caught, "%s: a snapshot in the wait was caught", name);
    if (!caught)
        return;
    const SceUID id = info->waitId;
    EventInfo event_info = { .size = sizeof(event_info) };
    const int event_result = sceKernelGetEventInfo(id, &event_info);
    PRINTF(info->status, "%s: B's status", name);
    PRINTF(info->waitType, "%s: B's wait type", name);
    PRINTF(id == -1, "%s: the wait id is -1", name);
    PRINTF(id == 0, "%s: the wait id is 0", name);
    PRINTF(id > 0, "%s: the wait id is positive", name);
    PRINTF(sceKernelGetThreadmgrUIDClass(id), "%s: the class of the wait id", name);
    PRINTF(event_result, "%s: sceKernelGetEventInfo on the wait id", name);
}

int main(void) {
    pin_main();
    static const char *const names[] = { "plain", "CB", "multi", "multi CB" };
    SceKernelThreadInfo infos[4] = {};
    int caught[4];
    for (int i = 0; i < 4; i++)
        caught[i] = catch_one(i, &infos[i]);
    SceKernelThreadInfo two1 = {}, two2 = {};
    const int caught_two = catch_two(&two1, &two2);

    for (int i = 0; i < 4; i++)
        print_info(names[i], caught[i], &infos[i]);
    PRINTF(caught_two, "two threads: both snapshots were caught at the same poll");
    if (caught_two) {
        PRINTF(two1.waitType == two2.waitType, "two threads: same wait type");
        PRINTF(two1.waitId == two2.waitId, "two threads: same wait id");
    }
    PRINTF(caught[VBLANK] && caught[VBLANK_CB] && infos[VBLANK].waitId == infos[VBLANK_CB].waitId, "same wait id in the plain and the CB wait");
    PRINTF(caught[MULTI] && caught[MULTI_CB] && infos[MULTI].waitId == infos[MULTI_CB].waitId, "same wait id in the multi and the multi CB wait");
    PRINTF(caught[VBLANK] && caught[MULTI] && infos[VBLANK].waitId == infos[MULTI].waitId, "same wait id in the plain and the multi wait");
    test_exit();
    return 0;
}
