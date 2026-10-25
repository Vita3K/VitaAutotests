// What a multiple event wait reports for an event listed under two of its UIDs. An openable simple
// event E is opened by its name and E's bit is set, then two AND waits list E twice: through the
// opened UID first and the creating UID second, then the other way round. Each prints the call's
// result and, for each result entry, whether its UID is the opened UID (1), the creating UID (2)
// or another (0). The test also prints whether the two UIDs differ.
//
// On a PS Vita TV on firmware 3.65 the two UIDs differ, both waits return 2, and both entries of a
// wait report the UID listed first.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID created, opened;

static int which(SceUID uid) {
    return uid == opened ? 1 : uid == created ? 2 : 0;
}

static void run(const char *name, SceUID first, SceUID second) {
    WaitEvent list[2] = { { first, EVENT_BIT_A }, { second, EVENT_BIT_A } };
    ResultEvent results[2];
    sceClibMemset(results, 0, sizeof(results));
    SceUInt32 timeout = 1000;
    PRINTF(sceKernelWaitMultipleEvents(list, 2, EVENT_WAIT_MODE_AND, results, &timeout), "%s: the call", name);
    PRINTF(which(results[0].eventId), "%s: the first entry's UID", name);
    PRINTF(which(results[1].eventId), "%s: the second entry's UID", name);
}

int main(void) {
    created = sceKernelCreateSimpleEvent("two handles", SCE_KERNEL_ATTR_OPENABLE | EVENT_ATTR_MANUAL_RESET, 0, NULL);
    opened = sceKernelOpenSimpleEvent("two handles");
    PRINT("open the event", test_created(opened));
    PRINT("the two UIDs differ", opened != created);
    sceKernelSetEvent(created, EVENT_BIT_A, 0);
    run("the opened UID first", opened, created);
    run("the creating UID first", created, opened);
    sceKernelCloseSimpleEvent(opened);
    sceKernelDeleteSimpleEvent(created);
    test_exit();
    return 0;
}
