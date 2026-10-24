// sceAppMgrReceiveSystemEvent while nothing is going on: what it returns for a NULL pointer, what it
// writes to the event, and whether polling it again and again keeps producing resume events.
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define POLLS 1000
#define FILL 0xAA
// Bytes of guard on each side of the event
#define GUARD 16

static struct {
    unsigned char before[GUARD];
    SceAppMgrSystemEvent event;
    unsigned char after[GUARD];
} buf;

static void fill(void) {
    sceClibMemset(&buf, FILL, sizeof(buf));
}

static int guards_intact(void) {
    for (int i = 0; i < GUARD; i++) {
        if (buf.before[i] != FILL || buf.after[i] != FILL)
            return 0;
    }
    return 1;
}

// How many bytes of the event are still the fill value
static int untouched(void) {
    const unsigned char *bytes = (const unsigned char *)&buf.event;
    int count = 0;
    for (int i = 0; i < (int)sizeof(buf.event); i++)
        count += bytes[i] == FILL;
    return count;
}

int main(void) {
    PRINT("size of the event", sizeof(SceAppMgrSystemEvent));

    // The first poll of the process, which may not be an idle one
    fill();
    const int first = sceAppMgrReceiveSystemEvent(&buf.event);
    PRINT("first poll", first);
    PRINT("first poll: event", buf.event.systemEvent);
    PRINT("first poll: untouched bytes", untouched());
    PRINT("first poll: guards intact", guards_intact());

    // Drain what is queued, then poll an idle process
    int drained = 0;
    int ret = 0;
    while (drained < POLLS) {
        fill();
        ret = sceAppMgrReceiveSystemEvent(&buf.event);
        if (ret != 0 || buf.event.systemEvent == 0)
            break;
        drained++;
    }
    PRINT("events before the process is idle, capped", drained);
    PRINT("idle poll", ret);

    fill();
    ret = sceAppMgrReceiveSystemEvent(&buf.event);
    PRINT("idle poll again", ret);
    PRINT("idle poll: event", buf.event.systemEvent);
    PRINT("idle poll: untouched bytes", untouched());
    PRINT("idle poll: guards intact", guards_intact());
    int reserved_zero = 1;
    for (int i = 0; i < (int)sizeof(buf.event.reserved); i++)
        reserved_zero &= buf.event.reserved[i] == 0;
    PRINT("idle poll: reserved bytes are all zero", reserved_zero);

    // Many idle polls
    int ok = 0;
    int resumes = 0;
    int events = 0;
    for (int i = 0; i < POLLS; i++) {
        fill();
        if (sceAppMgrReceiveSystemEvent(&buf.event) == 0)
            ok++;
        if (buf.event.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME)
            resumes++;
        if (buf.event.systemEvent != 0 && buf.event.systemEvent != (SceInt32)0xAAAAAAAA)
            events++;
    }
    PRINT("idle polls that succeed, of 1000", ok);
    PRINT("idle polls that give a resume event, of 1000", resumes);
    PRINT("idle polls that give any event, of 1000", events);

    // Last, in case it crashes
    PRINT("NULL event", sceAppMgrReceiveSystemEvent(NULL));
    test_exit();
    return 0;
}
