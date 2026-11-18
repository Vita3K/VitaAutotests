// The open limit option of a message pipe. An openable pipe is created with the limit N in the
// create option, then opened by name until an open fails. The count of opens that worked and the
// error are printed for each N. A limit of 0x80 is refused when the pipe is created.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define MAX_OPENS 0x90

// The create option with the open limit, as the firmware reads it
typedef struct PipeOpt {
    SceSize size;
    SceUInt32 flags;
    SceUInt32 reserved0;
    SceUInt32 reserved1;
    SceUInt32 open_limit;
} PipeOpt;

static SceUID opened[MAX_OPENS];

static void run(SceUInt32 limit) {
    PipeOpt opt = { sizeof(opt), 0x100, 0, 0, limit };
    const SceUID pipe = sceKernelCreateMsgPipe("open limit", MSG_PIPE_TYPE_USER, SCE_KERNEL_ATTR_OPENABLE, 0x1000, &opt);
    PRINTF(test_created(pipe), "limit 0x%X: create the pipe", limit);
    if (pipe < 0)
        return;
    int count = 0, error = 0;
    while (count < MAX_OPENS) {
        const SceUID id = sceKernelOpenMsgPipe("open limit");
        if (id < 0) {
            error = id;
            break;
        }
        opened[count++] = id;
    }
    PRINTF(error, "limit 0x%X: the error that ended the opens, 0 if the cap was reached", limit);
    PRINTF(count, "limit 0x%X: opens that worked", limit);
    int closed = 0;
    for (int i = 0; i < count; i++)
        closed += sceKernelCloseMsgPipe(opened[i]) == 0;
    PRINTF(closed == count, "limit 0x%X: every opened UID closed", limit);
    PRINTF(sceKernelDeleteMsgPipe(pipe), "limit 0x%X: delete the pipe", limit);
}

int main(void) {
    run(0);
    run(1);
    run(5);
    run(0x7F);
    run(0x80);

    // Without the option the pipe has no limit set by the option
    const SceUID plain = sceKernelCreateMsgPipe("open limit", MSG_PIPE_TYPE_USER, SCE_KERNEL_ATTR_OPENABLE, 0x1000, NULL);
    PRINT("no option: create the pipe", test_created(plain));
    int count = 0, error = 0;
    while (plain >= 0 && count < MAX_OPENS) {
        const SceUID id = sceKernelOpenMsgPipe("open limit");
        if (id < 0) {
            error = id;
            break;
        }
        opened[count++] = id;
    }
    PRINT("no option: the error that ended the opens, 0 if the cap was reached", error);
    PRINT("no option: opens that worked", count);
    for (int i = 0; i < count; i++)
        sceKernelCloseMsgPipe(opened[i]);
    if (plain >= 0)
        sceKernelDeleteMsgPipe(plain);

    test_exit();
    return 0;
}
