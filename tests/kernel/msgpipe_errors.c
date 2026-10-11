// Every message pipe call with a UID that isn't a message pipe it can use, and with arguments it
// should reject
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static char buf[8192];

int main(void) {
    const SceUID deleted = sceKernelCreateMsgPipe("deleted", MSG_PIPE_TYPE_USER, 0, 4096, NULL);
    sceKernelDeleteMsgPipe(deleted);
    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);
    const SceUID ids[] = { 0x12345, deleted, sema, 0, -1 };
    const char *const names[] = { "an unknown UID", "a deleted message pipe", "a semaphore", "UID 0", "UID -1" };

    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        unsigned int timeout = 1000;
        SceSize size = 0xDEAD;
        int senders = 0xDEAD, receivers = 0xDEAD;
        SceKernelMppInfo info = { .size = sizeof(info) };
        PRINTF(sceKernelSendMsgPipe(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size, &timeout), "send to %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelSendMsgPipeCB(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size, &timeout), "send with callbacks to %s", names[i]);
        PRINTF(sceKernelTrySendMsgPipe(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size), "try send to %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelReceiveMsgPipe(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size, &timeout), "receive from %s", names[i]);
        timeout = 1000;
        PRINTF(sceKernelReceiveMsgPipeCB(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size, &timeout), "receive with callbacks from %s", names[i]);
        PRINTF(sceKernelTryReceiveMsgPipe(ids[i], buf, 1, MSG_PIPE_MODE_ASAP, &size), "try receive from %s", names[i]);
        PRINTF(sceKernelCancelMsgPipe(ids[i], &senders, &receivers), "cancel %s", names[i]);
        PRINTF(sceKernelGetMsgPipeInfo(ids[i], &info), "info of %s", names[i]);
        PRINTF(sceKernelCloseMsgPipe(ids[i]), "close %s", names[i]);
        PRINTF(sceKernelDeleteMsgPipe(ids[i]), "delete %s", names[i]);
    }
    PRINT("the semaphore survives the message pipe calls", sceKernelPollSema(sema, 1));

    // Creation
    PRINT("create with an empty name", test_created(sceKernelCreateMsgPipe("", MSG_PIPE_TYPE_USER, 0, 4096, NULL)));
    PRINT("create with attribute 0x80000000", test_created(sceKernelCreateMsgPipe("p", MSG_PIPE_TYPE_USER, 0x80000000, 4096, NULL)));
    PRINT("create with type 0x20", test_created(sceKernelCreateMsgPipe("p", 0x20, 0, 4096, NULL)));
    PRINT("create with type 0x80", test_created(sceKernelCreateMsgPipe("p", 0x80, 0, 4096, NULL)));
    PRINT("create with type 0xFF", test_created(sceKernelCreateMsgPipe("p", 0xFF, 0, 4096, NULL)));
    PRINT("create with 4 KiB plus 1", test_created(sceKernelCreateMsgPipe("p", MSG_PIPE_TYPE_USER, 0, 4097, NULL)));
    PRINT("create with 1 MiB", test_created(sceKernelCreateMsgPipe("p", MSG_PIPE_TYPE_USER, 0, 0x100000, NULL)));

    // Sizes past what the pipe holds
    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 4096, NULL);
    SceSize size = 0xDEAD;
    PRINT("try send FULL of more than the pipe holds", sceKernelTrySendMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size));
    PRINT("its size", size);
    size = 0xDEAD;
    unsigned int timeout = 1000;
    PRINT("send FULL of more than the pipe holds", sceKernelSendMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size, &timeout));
    PRINT("its size", size);
    size = 0xDEAD;
    PRINT("try send ASAP of more than the pipe holds", sceKernelTrySendMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_ASAP, &size));
    PRINT("its size", size);
    size = 0xDEAD;
    PRINT("try receive FULL of more than the pipe holds", sceKernelTryReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size));
    PRINT("its size", size);
    size = 0xDEAD;
    PRINT("try receive ASAP of more than the pipe holds", sceKernelTryReceiveMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_ASAP, &size));
    PRINT("its size", size);
    PRINT("try send with mode 0x200", sceKernelTrySendMsgPipe(pipe, buf, 1, 0x200, &size));
    PRINT("try send with DONT_REMOVE", sceKernelTrySendMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_DONT_REMOVE, &size));

    // Info
    SceKernelMppInfo info = { .size = 0 };
    PRINT("info with a size of 0", sceKernelGetMsgPipeInfo(pipe, &info));
    // With room behind the struct, in case the call writes as much as the size says
    static union {
        SceKernelMppInfo info;
        unsigned char bytes[sizeof(SceKernelMppInfo) + 64];
    } big;
    sceClibMemset(&big, 0xEE, sizeof(big));
    big.info.size = sizeof(SceKernelMppInfo) + 4;
    PRINT("info with a size past the struct", sceKernelGetMsgPipeInfo(pipe, &big.info));
    int untouched = 1;
    for (unsigned int i = sizeof(SceKernelMppInfo); i < sizeof(big.bytes); i++)
        untouched &= big.bytes[i] == 0xEE;
    PRINT("it leaves the bytes past the struct alone", untouched);

    // NULL pointers and a huge size last, so an implementation that crashes on them loses nothing
    // else
    PRINT("create with a huge size", test_created(sceKernelCreateMsgPipe("p", MSG_PIPE_TYPE_USER, 0, 0x7FFFFFFF, NULL)));
    PRINT("try send without a size pointer", sceKernelTrySendMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, NULL));
    PRINT("try send from a NULL buffer", sceKernelTrySendMsgPipe(pipe, NULL, 1, MSG_PIPE_MODE_ASAP, &size));
    PRINT("try receive into a NULL buffer", sceKernelTryReceiveMsgPipe(pipe, NULL, 1, MSG_PIPE_MODE_ASAP, &size));
    PRINT("info with a NULL pointer", sceKernelGetMsgPipeInfo(pipe, NULL));
    PRINT("open with a NULL name", sceKernelOpenMsgPipe(NULL));
    PRINT("create with a NULL name", test_created(sceKernelCreateMsgPipe(NULL, MSG_PIPE_TYPE_USER, 0, 4096, NULL)));

    test_exit();
    return 0;
}
