// Which memory types sceKernelCreateMsgPipe accepts. Each type creates a
// message pipe of 4 KiB with no attributes, and prints the error the create
// returned, or 0 when it created the pipe. The pipes that were created are
// deleted at the end. msgpipe_edges covers the create sizes and attributes, and
// msgpipe_errors covers the other calls on bad UIDs, but neither tries the
// memory type. The types are 0x40, which games pass, 0x41, 0x42, 0x0 and 0x80.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

int main(void) {
    static const unsigned int types[] = { 0x40, 0x41, 0x42, 0x0, 0x80 };
    SceUID pipes[sizeof(types) / sizeof(types[0])];

    for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        pipes[i] = sceKernelCreateMsgPipe("types", types[i], 0, 4096, NULL);
        PRINTF(test_created(pipes[i]), "create with type 0x%X", types[i]);
    }

    for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (pipes[i] >= 0)
            sceKernelDeleteMsgPipe(pipes[i]);
    }

    test_exit();
    return 0;
}
