// How many message pipe buffer bytes a process may hold at once. The test fills the process up to
// the limit with 1 MiB pipes, frees one and fills the rest with one page pipes, and from that full
// state frees one page and one 1 MiB pipe to see that the total counts whole 4 KiB pages. The output
// doesn't depend on how much the process already holds when the test starts.
//
// On a PS Vita TV on firmware 3.65 a process may hold 32 MiB of pipe buffers, counted in whole
// pages, and a create past that fails with 0x8002000B. The test app already holds 28 pages when it
// starts, pipes it didn't create, so 31 1 MiB pipes and 228 page pipes fill it, and a single pipe of
// 32 MiB fails even with none of the test's pipes left (seen with an earlier form of this test).
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define PAGE 0x1000
#define MIB 0x100000
#define MAX_PIPES 700

static SceUID pipes[MAX_PIPES];
static int count;

static SceUID create(SceSize size) {
    return sceKernelCreateMsgPipe("total", MSG_PIPE_TYPE_USER, 0, size, NULL);
}

// Creates pipes of size until a create fails or limit pipes were made. Prints the error that
// stopped it and returns how many it made.
static int fill(const char *label, SceSize size, int limit) {
    int made = 0;
    int error = 0;
    while (made < limit && count < MAX_PIPES) {
        const SceUID pipe = create(size);
        if (pipe < 0) {
            error = pipe;
            break;
        }
        pipes[count++] = pipe;
        made++;
    }
    PRINTF(error, "%s: the error that stopped it", label);
    return made;
}

// Creates a pipe of size and keeps it, printing the create's result
static void create_kept(const char *label, SceSize size) {
    const SceUID pipe = create(size);
    PRINT(label, test_created(pipe));
    if (pipe >= 0)
        pipes[count++] = pipe;
}

// Creates a pipe of size and deletes it at once, printing the create's result
static void create_deleted(const char *label, SceSize size) {
    const SceUID pipe = create(size);
    PRINT(label, test_created(pipe));
    if (pipe >= 0)
        sceKernelDeleteMsgPipe(pipe);
}

static void delete_last(void) {
    sceKernelDeleteMsgPipe(pipes[--count]);
}

int main(void) {
    // Deleting the last 1 MiB pipe leaves room for at least 256 page pipes
    const int mib_pipes = fill("1 MiB pipes", MIB, 40);
    // Nothing to free when not even one 1 MiB pipe fits
    if (mib_pipes == 0) {
        test_exit();
        return 0;
    }
    delete_last();
    const int page_pipes = fill("page pipes after deleting one of them", PAGE, 600);
    const unsigned int filled = (mib_pipes - 1) * MIB + page_pipes * PAGE;
    PRINT("the fill made at least 31 MiB and at most 32 MiB", filled >= 31 * MIB && filled <= 32 * MIB);

    // A page frees room for one page but not for two
    delete_last();
    create_deleted("after deleting a page pipe: a pipe of 2 pages", 2 * PAGE);
    create_kept("after deleting a page pipe: a pipe of 1 page", PAGE);

    // A 1 MiB pipe frees room for 1 MiB, and a size of 1 MiB plus one byte takes a page more
    sceKernelDeleteMsgPipe(pipes[0]);
    pipes[0] = pipes[--count];
    create_deleted("after deleting a 1 MiB pipe: a pipe of 1 MiB plus 1 byte", MIB + 1);
    // A size of 1 MiB minus 0xFFF bytes takes all of the 1 MiB, so a 1 byte pipe doesn't fit
    create_kept("after deleting a 1 MiB pipe: a pipe of 1 MiB minus 0xFFF bytes", MIB - 0xFFF);
    create_deleted("then a pipe of 1 byte", 1);
    delete_last();
    create_deleted("after deleting that: a pipe of 1 MiB", MIB);

    while (count > 0)
        delete_last();

    // A pipe of 0x1001 bytes holds two pages
    const SceUID pipe = create(PAGE + 1);
    SceKernelMppInfo info = { .size = sizeof(info) };
    PRINT("info of a pipe of 0x1001 bytes", sceKernelGetMsgPipeInfo(pipe, &info));
    PRINT("its buffer size", info.bufSize);
    PRINT("its free size", info.freeSize);
    sceKernelDeleteMsgPipe(pipe);

    test_exit();
    return 0;
}
