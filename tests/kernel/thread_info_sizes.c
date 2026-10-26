// What sceKernelGetThreadInfo does with the size field of its struct, for three threads: the
// running one (UID 0), one that has ended and was not deleted, and one that waits on a semaphore.
// Each gets the sizes 0, 1, 2, 3, 4, 8, 0x10, 0x7C, 0x7F and 0x80, then the sizes 0x81 and 0x100,
// then a NULL pointer, in that order. Each call fills two buffers with room behind them, one
// prefilled with 0xCC and one with 0x33, so a byte the call writes differs from its marker whatever
// its value. For each size the test prints:
// - the result, and nothing more if the call fails
// - the bytes written, counted up to the last byte that changed, after the size word
// - the size word the call left in the first buffer
// - whether the last word of the struct is 0 in the first buffer
// - whether the last byte of the name is 0 in the first buffer
// - whether the call changed no byte past the size it was given
// The waiting thread is woken after the probes, and the ended thread is deleted last.
#include <stddef.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define BUF_SIZE 0x100
#define RESERVED_OFFSET offsetof(SceKernelThreadInfo, reserved)
#define NAME_LAST_OFFSET (offsetof(SceKernelThreadInfo, name) + 31)

static SceUID sema;
static unsigned char first[BUF_SIZE] __attribute__((aligned(8)));
static unsigned char second[BUF_SIZE] __attribute__((aligned(8)));

static int ended_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

static int wait_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 5 * TEST_WAIT_US;
    sceKernelWaitSema(sema, 1, &timeout);
    return 0;
}

// Calls sceKernelGetThreadInfo on thread with size as the size field, once into each buffer, and
// prints what the first call did to its buffer
static void probe(const char *name, SceUID thread, SceSize size) {
    sceClibMemset(first, 0xCC, sizeof(first));
    sceClibMemset(second, 0x33, sizeof(second));
    sceClibMemcpy(first, &size, sizeof(size));
    sceClibMemcpy(second, &size, sizeof(size));
    const int ret = sceKernelGetThreadInfo(thread, (SceKernelThreadInfo *)first);
    sceKernelGetThreadInfo(thread, (SceKernelThreadInfo *)second);
    PRINTF(ret, "%s info with a size of 0x%X", name, (unsigned int)size);
    if (ret < 0)
        return;

    int written = 0;
    for (int i = sizeof(SceSize); i < BUF_SIZE; i++)
        if (first[i] != 0xCC || second[i] != 0x33)
            written = i + 1;
    PRINTF(written, "%s bytes written with a size of 0x%X", name, (unsigned int)size);

    SceSize size_after;
    sceClibMemcpy(&size_after, first, sizeof(size_after));
    PRINTF(size_after, "%s size word left with a size of 0x%X", name, (unsigned int)size);

    SceInt32 reserved;
    sceClibMemcpy(&reserved, first + RESERVED_OFFSET, sizeof(reserved));
    PRINTF(reserved == 0, "%s last word is 0 with a size of 0x%X", name, (unsigned int)size);
    PRINTF(first[NAME_LAST_OFFSET] == 0, "%s name[31] is 0 with a size of 0x%X", name,
           (unsigned int)size);
    PRINTF(written <= (int)size, "%s nothing past the size written with a size of 0x%X", name,
           (unsigned int)size);
}

int main(void) {
    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    const SceUID ended = test_start_thread(ended_entry);
    SceUInt32 end_timeout = TEST_WAIT_US;
    sceKernelWaitThreadEnd(ended, NULL, &end_timeout);
    const SceUID waiter = test_start_thread(wait_entry);
    TEST_WAIT_FOR(test_in_wait(waiter, WAIT_TYPE_SEMA, sema));

    const SceUID threads[] = { 0, ended, waiter };
    const char *const names[] = { "the running thread", "an ended thread", "a waiting thread" };
    static const SceSize sizes[] = { 0, 1, 2, 3, 4, 8, 0x10, 0x7C, 0x7F, 0x80 };
    static const SceSize past_struct[] = { 0x81, 0x100 };
    const unsigned int thread_count = sizeof(threads) / sizeof(threads[0]);

    for (unsigned int t = 0; t < thread_count; t++)
        for (unsigned int s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
            probe(names[t], threads[t], sizes[s]);
    for (unsigned int t = 0; t < thread_count; t++)
        for (unsigned int s = 0; s < sizeof(past_struct) / sizeof(past_struct[0]); s++)
            probe(names[t], threads[t], past_struct[s]);
    for (unsigned int t = 0; t < thread_count; t++)
        PRINTF(sceKernelGetThreadInfo(threads[t], NULL), "%s info with a NULL pointer", names[t]);

    sceKernelSignalSema(sema, 1);
    SceUInt32 join_timeout = 5 * TEST_WAIT_US;
    sceKernelWaitThreadEnd(waiter, NULL, &join_timeout);
    sceKernelDeleteThread(waiter);
    sceKernelDeleteThread(ended);
    sceKernelDeleteSema(sema);
    test_exit();
    return 0;
}
