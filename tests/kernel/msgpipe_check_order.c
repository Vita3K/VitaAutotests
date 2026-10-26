// Which error a message pipe call reports first when several of its arguments are wrong at once.
// Each combination of faults in the table is run through every send and receive form: the blocking
// and try calls, with and without callbacks, the plain calls that take one buffer and the vector
// calls that take a list of pieces. A form only takes the faults it has, so the list and count
// faults only reach the vector forms, and each bad mode only reaches the blocking form it is bad
// for. Every combination runs on an unknown UID, a semaphore, a deleted message pipe and a live
// message pipe. The live pipe is made for each combination and cancelled and deleted at its end.
// Every call is wrong by its arguments or by its UID, so none waits for a transfer, and the 1 ms
// timeouts only bound a wrong implementation. The result of each call is printed. The size is
// preset to 0xDEAD, and the test prints whether any call of each form wrote it. This file runs the
// cases without a NULL pointer. msgpipe_check_order_null.c includes it with the cases that have
// one, since a wrong implementation may crash on those.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define CAPACITY 4096
#define NOT_WRITTEN 0xDEAD
#define TIMEOUT_US 1000
#define UIDS 4

enum Form {
    FORM_SEND,
    FORM_SEND_CB,
    FORM_TRY_SEND,
    FORM_RECEIVE,
    FORM_RECEIVE_CB,
    FORM_TRY_RECEIVE,
    FORM_VECTOR_SEND,
    FORM_VECTOR_SEND_CB,
    FORM_TRY_VECTOR_SEND,
    FORM_VECTOR_RECEIVE,
    FORM_VECTOR_RECEIVE_CB,
    FORM_TRY_VECTOR_RECEIVE,
    FORM_COUNT,
};

static const char *const form_names[FORM_COUNT] = {
    "send",
    "send with callbacks",
    "try send",
    "receive",
    "receive with callbacks",
    "try receive",
    "vector send",
    "vector send with callbacks",
    "try vector send",
    "vector receive",
    "vector receive with callbacks",
    "try vector receive",
};

// A set of faults. A NULL list and a count of 0 only apply to the vector forms. The mode 0x100
// is bad for a blocking send, and 0x2 and 0x1000 for a blocking receive. 0x200 is bad for all.
typedef struct Case {
    const char *name;
    int null_list;
    int zero_count;
    SceUInt32 mode;
    int null_buffer;
    SceSize piece_size;
} Case;

// The cases with a NULL pointer are listed last
static const Case cases[] = {
    { "count 0", 0, 1, MSG_PIPE_MODE_FULL, 0, 4 },
    { "mode 0x200", 0, 0, 0x200, 0, 4 },
    { "size 4097", 0, 0, MSG_PIPE_MODE_FULL, 0, 4097 },
    { "mode 0x100", 0, 0, MSG_PIPE_MODE_DONT_REMOVE, 0, 4 },
    { "mode 0x2", 0, 0, 0x2, 0, 4 },
    { "mode 0x1000", 0, 0, 0x1000, 0, 4 },
    { "mode 0x200 and size 4097", 0, 0, 0x200, 0, 4097 },
    { "mode 0x200 and NULL buffer", 0, 0, 0x200, 1, 4 },
    { "NULL buffer", 0, 0, MSG_PIPE_MODE_FULL, 1, 4 },
    { "count 0, mode 0x200 and NULL buffer", 0, 1, 0x200, 1, 4 },
    { "NULL list, count 0, mode 0x200 and NULL buffer", 1, 1, 0x200, 1, 4 },
    { "NULL list", 1, 0, MSG_PIPE_MODE_FULL, 0, 4 },
};
#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))

// Set to 1 by msgpipe_check_order_null.c, which runs the cases with a NULL pointer
#ifndef CHECK_ORDER_WITH_NULL
#define CHECK_ORDER_WITH_NULL 0
#endif

static char buf[8192];
static SceUID deleted, sema;
static const char *const names[UIDS] = { "an unknown UID", "a semaphore", "a deleted message pipe", "a live message pipe" };
static int size_written[FORM_COUNT];

static int has_null_pointer(const Case *c) {
    return c->null_list || c->null_buffer;
}

static int is_vector(int form) {
    return form >= FORM_VECTOR_SEND;
}

static int is_blocking_send(int form) {
    return form == FORM_SEND || form == FORM_SEND_CB || form == FORM_VECTOR_SEND || form == FORM_VECTOR_SEND_CB;
}

static int is_blocking_receive(int form) {
    return form == FORM_RECEIVE || form == FORM_RECEIVE_CB || form == FORM_VECTOR_RECEIVE || form == FORM_VECTOR_RECEIVE_CB;
}

// Whether the form can take every fault of the case
static int applies(int form, const Case *c) {
    if ((c->null_list || c->zero_count) && !is_vector(form))
        return 0;
    if (c->mode == MSG_PIPE_MODE_DONT_REMOVE && !is_blocking_send(form))
        return 0;
    if ((c->mode == 0x2 || c->mode == 0x1000) && !is_blocking_receive(form))
        return 0;
    return 1;
}

// Calls the form on id with the faults of c, which writes its size to size if it gets that far
static SceInt32 call_form(int form, SceUID id, const Case *c, SceSize *size) {
    SceUInt32 timeout = TIMEOUT_US;
    MsgPipeVector piece = { c->null_buffer ? NULL : buf, c->piece_size };
    const MsgPipeVector *list = c->null_list ? NULL : &piece;
    const SceUInt32 count = c->zero_count ? 0 : 1;
    switch (form) {
    case FORM_SEND:
        return sceKernelSendMsgPipe(id, piece.base, piece.size, c->mode, size, &timeout);
    case FORM_SEND_CB:
        return sceKernelSendMsgPipeCB(id, piece.base, piece.size, c->mode, size, &timeout);
    case FORM_TRY_SEND:
        return sceKernelTrySendMsgPipe(id, piece.base, piece.size, c->mode, size);
    case FORM_RECEIVE:
        return sceKernelReceiveMsgPipe(id, piece.base, piece.size, c->mode, size, &timeout);
    case FORM_RECEIVE_CB:
        return sceKernelReceiveMsgPipeCB(id, piece.base, piece.size, c->mode, size, &timeout);
    case FORM_TRY_RECEIVE:
        return sceKernelTryReceiveMsgPipe(id, piece.base, piece.size, c->mode, size);
    case FORM_VECTOR_SEND:
        return sceKernelSendMsgPipeVector(id, list, count, c->mode, size, &timeout);
    case FORM_VECTOR_SEND_CB:
        return sceKernelSendMsgPipeVectorCB(id, list, count, c->mode, size, &timeout);
    case FORM_TRY_VECTOR_SEND:
        return sceKernelTrySendMsgPipeVector(id, list, count, c->mode, size);
    case FORM_VECTOR_RECEIVE:
        return sceKernelReceiveMsgPipeVector(id, list, count, c->mode, size, &timeout);
    case FORM_VECTOR_RECEIVE_CB:
        return sceKernelReceiveMsgPipeVectorCB(id, list, count, c->mode, size, &timeout);
    case FORM_TRY_VECTOR_RECEIVE:
        return sceKernelTryReceiveMsgPipeVector(id, list, count, c->mode, size);
    }
    return 0;
}

// Runs the case on every form that can take its faults, each on the four UIDs
static void run_case(const Case *c) {
    for (int form = 0; form < FORM_COUNT; form++) {
        if (!applies(form, c))
            continue;
        const SceUID live = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
        const SceUID ids[UIDS] = { 0x12345, sema, deleted, live };
        for (int u = 0; u < UIDS; u++) {
            SceSize size = NOT_WRITTEN;
            const SceInt32 result = call_form(form, ids[u], c, &size);
            PRINTF(result, "%s, %s, %s", form_names[form], c->name, names[u]);
            size_written[form] |= (size != NOT_WRITTEN);
        }
        int senders = 0, receivers = 0;
        sceKernelCancelMsgPipe(live, &senders, &receivers);
        sceKernelDeleteMsgPipe(live);
    }
}

int main(void) {
    static const int with_null = CHECK_ORDER_WITH_NULL;
    const char *const group = with_null ? "with NULL pointers" : "without NULL pointers";
    deleted = sceKernelCreateMsgPipe("deleted", MSG_PIPE_TYPE_USER, 0, CAPACITY, NULL);
    sceKernelDeleteMsgPipe(deleted);
    sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);

    for (unsigned int c = 0; c < CASE_COUNT; c++) {
        if (has_null_pointer(&cases[c]) == with_null)
            run_case(&cases[c]);
    }
    for (int form = 0; form < FORM_COUNT; form++)
        PRINTF(!size_written[form], "%s: %s, no call wrote its size", form_names[form], group);
    PRINTF(sceKernelPollSema(sema, 1), "the semaphore survives the calls %s", group);
    test_exit();
    return 0;
}
