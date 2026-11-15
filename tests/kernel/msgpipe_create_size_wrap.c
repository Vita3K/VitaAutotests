// What a message pipe create returns for a size that rounds up past 4 GiB. A pipe's buffer is a
// whole number of 4 KiB pages, and these sizes round up to 0x100000000. Each create that succeeds is
// deleted at once.
//
// On a PS Vita TV on firmware 3.65 every one of these creates fails with 0x8002000B, the same error
// as a size of 0.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static void create(const char *label, SceSize size) {
    const SceUID pipe = sceKernelCreateMsgPipe("wrap", MSG_PIPE_TYPE_USER, 0, size, NULL);
    PRINT(label, test_created(pipe));
    if (pipe >= 0)
        sceKernelDeleteMsgPipe(pipe);
}

int main(void) {
    create("create with a size of 0xFFFFF001", 0xFFFFF001);
    create("create with a size of 0xFFFFF800", 0xFFFFF800);
    create("create with a size of 0xFFFFFFFF", 0xFFFFFFFF);

    test_exit();
    return 0;
}
