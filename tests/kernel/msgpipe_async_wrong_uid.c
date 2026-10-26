// DONT_WAIT message pipe sends to UIDs that aren't a message pipe: an unknown UID, and a semaphore
// with count 1. Each send has its own size variable preset to 0xDEAD. The test prints each result
// and each size variable, then waits 100 ms and prints the semaphore's count, to show whether the
// send to it changed the semaphore. The cases are:
// - a DONT_WAIT FULL send of 4 bytes to the unknown UID 0x12345.
// - a DONT_WAIT FULL send of 4 bytes to a semaphore created with count 1 and maximum count 1.
// This test stands alone, since an implementation that mishandles a UID that isn't a message pipe
// may crash on it.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    static char msg[4] = { 1, 2, 3, 4 };
    SceSize unknown_size = 0xDEAD;
    SceSize sema_size = 0xDEAD;

    const SceUID sema = sceKernelCreateSema("sema", 0, 1, 1, NULL);

    PRINT("DONT_WAIT FULL send to an unknown UID", sceKernelSendMsgPipe(0x12345, msg, sizeof(msg), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &unknown_size, NULL));
    PRINT("its size", unknown_size);
    PRINT("DONT_WAIT FULL send to a semaphore", sceKernelSendMsgPipe(sema, msg, sizeof(msg), MSG_PIPE_MODE_FULL | MSG_PIPE_MODE_DONT_WAIT, &sema_size, NULL));
    PRINT("its size", sema_size);

    sceKernelDelayThread(100000);
    SceKernelSemaInfo info = { .size = sizeof(info) };
    PRINT("the semaphore's info", sceKernelGetSemaInfo(sema, &info));
    PRINT("the semaphore's count 100 ms later", info.currentCount);

    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
