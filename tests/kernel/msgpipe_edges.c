// Message pipe edge cases
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    // A Vita rounds a pipe up to 4 KiB, so the pipe and the buffer that fills it are that size
    static char buf[4096];
    SceSize size;
    unsigned int got;
    SceUInt timeout;

    PRINT("create with a size of 0", test_created(sceKernelCreateMsgPipe("zero", MSG_PIPE_TYPE_USER, 0, 0, NULL)));
    PRINT("create with a size of 1", test_created(sceKernelCreateMsgPipe("one", MSG_PIPE_TYPE_USER, 0, 1, NULL)));
    PRINT("create with an odd size", test_created(sceKernelCreateMsgPipe("odd", MSG_PIPE_TYPE_USER, 0, 13, NULL)));
    PRINT("create with type 0", test_created(sceKernelCreateMsgPipe("type0", 0, 0, 16, NULL)));

    // Which attributes a message pipe takes, the PSP had separate sender and receiver queue orders
    static const unsigned int attrs[] = { 0x100, 0x1000, 0x1100, 0x2000, 0x2100, 0x4000, 0x80, 0x8, 0x1 };
    for (unsigned int i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        char label[64];
        sceClibSnprintf(label, sizeof(label), "create with attribute 0x%X", attrs[i]);
        PRINT(label, test_created(sceKernelCreateMsgPipe("attr", MSG_PIPE_TYPE_USER, attrs[i], 16, NULL)));
    }

    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, sizeof(buf), NULL);

    // Zero bytes on an empty pipe
    size = 0xDEAD;
    PRINT("try send of 0 bytes FULL on an empty pipe", sceKernelTrySendMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_FULL, &size));
    PRINT("its size", size);
    size = 0xDEAD;
    PRINT("try send of 0 bytes ASAP on an empty pipe", sceKernelTrySendMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_ASAP, &size));
    PRINT("its size", size);
    size = 0xDEAD;
    PRINT("try receive of 0 bytes ASAP on an empty pipe", sceKernelTryReceiveMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_ASAP, &size));
    PRINT("its size", size);
    got = 0xDEAD;
    timeout = 1000;
    PRINT("receive of 0 bytes on an empty pipe", sceKernelReceiveMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_FULL, &got, &timeout));
    PRINT("its size", got);

    // Zero bytes on a full pipe
    sceKernelTrySendMsgPipe(pipe, buf, sizeof(buf), MSG_PIPE_MODE_FULL, &size);
    size = 0xDEAD;
    PRINT("try send of 0 bytes FULL on a full pipe", sceKernelTrySendMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_FULL, &size));
    PRINT("its size", size);
    got = 0xDEAD;
    timeout = 1000;
    PRINT("send of 0 bytes on a full pipe", sceKernelSendMsgPipe(pipe, buf, 0, MSG_PIPE_MODE_FULL, &got, &timeout));
    PRINT("its size", got);

    // Wait mode bits that aren't documented for these calls
    PRINT("try receive with mode 0x2", sceKernelTryReceiveMsgPipe(pipe, buf, 1, 0x2, &size));
    PRINT("try receive with mode 0x1000", sceKernelTryReceiveMsgPipe(pipe, buf, 1, 0x1000, &size));
    timeout = 1000;
    PRINT("receive with DONT_WAIT", sceKernelReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP | 0x10, &got, &timeout));

    // Results and timeouts left out
    PRINT("try receive without a size pointer", sceKernelTryReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, NULL));
    PRINT("cancel without count pointers", sceKernelCancelMsgPipe(pipe, NULL, NULL));
    PRINT("try receive after the cancel", sceKernelTryReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_ASAP, &size));

    test_exit();
    return 0;
}
