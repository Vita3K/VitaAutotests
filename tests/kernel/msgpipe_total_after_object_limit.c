// A pipe create refused by the plain object limit may keep its bytes counted in the pipe byte
// total. The semaphores fill the plain object limit, then 1 MiB pipe creates are tried 40 times and
// their errors are counted. After the semaphores are deleted, 1 MiB pipes are created until one
// fails, and the count and the error are printed.
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define SEMA_CAP 4096
#define REFUSED_TRIES 40
#define PIPE_CAP 64
#define MIB 0x100000

#define ERROR_OBJECT_LIMIT ((int)0x8002450D)
#define ERROR_SIZE ((int)0x8002000B)

static SceUID semas[SEMA_CAP];
static SceUID pipes[PIPE_CAP];

int main(void) {
    const SceUID first = sceKernelCreateMsgPipe("after", MSG_PIPE_TYPE_USER, 0, 0x1000, NULL);
    PRINT("create a small pipe first", test_created(first));
    sceKernelDeleteMsgPipe(first);

    int sema_count = 0, sema_error = 0;
    while (sema_count < SEMA_CAP) {
        const SceUID sema = sceKernelCreateSema("fill", 0, 0, 1, NULL);
        if (sema < 0) {
            sema_error = sema;
            break;
        }
        semas[sema_count++] = sema;
    }
    PRINT("the error that ended the semaphores", sema_error);

    int object_limit = 0, size_error = 0, other = 0, worked = 0;
    for (int i = 0; i < REFUSED_TRIES; i++) {
        const SceUID pipe = sceKernelCreateMsgPipe("refused", MSG_PIPE_TYPE_USER, 0, MIB, NULL);
        if (pipe >= 0) {
            worked++;
            sceKernelDeleteMsgPipe(pipe);
        } else if (pipe == ERROR_OBJECT_LIMIT) {
            object_limit++;
        } else if (pipe == ERROR_SIZE) {
            size_error++;
        } else {
            other++;
        }
    }
    PRINT("pipe creates that worked at the limit", worked);
    PRINT("pipe creates refused by the object limit", object_limit);
    PRINT("pipe creates refused by the size", size_error);
    PRINT("pipe creates refused by anything else", other);

    int deleted = 0;
    for (int i = 0; i < sema_count; i++)
        deleted += sceKernelDeleteSema(semas[i]) == 0;
    PRINT("every semaphore deleted", deleted == sema_count);

    int count = 0, error = 0;
    while (count < PIPE_CAP) {
        const SceUID pipe = sceKernelCreateMsgPipe("after", MSG_PIPE_TYPE_USER, 0, MIB, NULL);
        if (pipe < 0) {
            error = pipe;
            break;
        }
        pipes[count++] = pipe;
    }
    PRINT("the error that ended the pipe creates, 0 if the cap was reached", error);
    PRINT("1 MiB pipes created after the semaphores are gone", count);
    deleted = 0;
    for (int i = 0; i < count; i++)
        deleted += sceKernelDeleteMsgPipe(pipes[i]) == 0;
    PRINT("every pipe deleted", deleted == count);

    test_exit();
    return 0;
}
