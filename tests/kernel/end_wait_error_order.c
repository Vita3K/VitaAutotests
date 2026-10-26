// The results of sceKernelWaitThreadEnd and sceKernelWaitThreadEndCB for UIDs that can't be
// waited on: UID 0, an unknown UID, and the caller's own UID. Each call gets an exit status pointer
// that is valid or NULL, and the status the call wrote is printed when the pointer is valid. Each
// call is made with and without callbacks. The calls come in two groups, so the one that could hang
// a wrong implementation comes last:
// - UID 0, an unknown UID and the caller's own UID, each with a timeout of 1 second. The own UID
//   always gets a timeout, so a wrong implementation can't wait forever on it.
// - UID 0 and an unknown UID with no timeout, last, since a wrong implementation might wait forever
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define UNKNOWN_UID 0x12345
#define TIMEOUT_US 1000000

enum Who { ZERO_UID, UNKNOWN, OWN };

static const char *const who_names[] = { "UID 0", "an unknown UID", "its own UID" };

static SceUID uid_of(int who) {
    switch (who) {
    case ZERO_UID:
        return 0;
    case UNKNOWN:
        return UNKNOWN_UID;
    case OWN:
        return sceKernelGetThreadId();
    }
    return 0;
}

// Makes one end wait, with or without callbacks, and prints its result and the status it wrote
static void run_case(int with_cb, int who, int with_status, int with_timeout) {
    int status = 0xDEAD;
    SceUInt32 timeout = TIMEOUT_US;
    const SceUID uid = uid_of(who);
    int *status_ptr = with_status ? &status : NULL;
    SceUInt32 *timeout_ptr = with_timeout ? &timeout : NULL;
    const char *call = with_cb ? "CB wait" : "wait";
    const char *pointers = with_status ? (with_timeout ? "status and timeout valid" : "status valid, no timeout")
                                       : (with_timeout ? "no status, timeout valid" : "no status or timeout");

    const int result = with_cb ? sceKernelWaitThreadEndCB(uid, status_ptr, timeout_ptr)
                               : sceKernelWaitThreadEnd(uid, status_ptr, timeout_ptr);
    PRINTF(result, "%s on %s, %s: the result", call, who_names[who], pointers);
    if (with_status)
        PRINTF(status, "%s on %s, %s: the status", call, who_names[who], pointers);
}

int main(void) {
    // Group one: UID 0, an unknown UID and then the caller's own UID, each with a timeout
    for (int with_cb = 0; with_cb < 2; with_cb++)
        for (int who = ZERO_UID; who <= UNKNOWN; who++)
            for (int with_status = 1; with_status >= 0; with_status--)
                run_case(with_cb, who, with_status, 1);
    for (int with_cb = 0; with_cb < 2; with_cb++)
        for (int with_status = 1; with_status >= 0; with_status--)
            run_case(with_cb, OWN, with_status, 1);

    // Group two, last: no timeout for UID 0 and an unknown UID
    for (int with_cb = 0; with_cb < 2; with_cb++)
        for (int who = ZERO_UID; who <= UNKNOWN; who++)
            for (int with_status = 1; with_status >= 0; with_status--)
                run_case(with_cb, who, with_status, 0);

    test_exit();
    return 0;
}
