// Delays of 2^31 and up, plain, with callbacks and their 200 variants. Each delay runs in its own
// thread, and the threads are left delaying when the test ends, as delay_negative does, for the
// process exit to end them, so the test never joins them. After 300 ms the test prints the wait
// type each thread shows, which says whether it is still in a delay.
// Cases: the four delays, each with 0x7FFFFFFF, 0x80000000 and 0xFFFFFFFE.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

enum Kind { PLAIN, CB, PLAIN_200, CB_200, KIND_COUNT };
static const char *const kind_names[] = {
    "delay", "delay with callbacks", "delay 200", "delay 200 with callbacks"
};

static const SceUInt32 delays[] = { 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE };
#define DELAY_COUNT 3

static int delay(int kind, SceUInt32 us) {
    switch (kind) {
    case PLAIN: return sceKernelDelayThread(us);
    case CB: return sceKernelDelayThreadCB(us);
    case PLAIN_200: return sceKernelDelayThread200(us);
    default: return sceKernelDelayThreadCB200(us);
    }
}

// Delays in the form and for the length that its index in the case list picks
static int delay_thread(SceSize args, void *argp) {
    (void)args;
    const int index = *(const int *)argp;
    delay(index / DELAY_COUNT, delays[index % DELAY_COUNT]);
    return 0;
}

static SceUInt32 wait_type(SceUID thread) {
    SceKernelThreadInfo info = { .size = sizeof(info) };
    return sceKernelGetThreadInfo(thread, &info) == 0 ? info.waitType : 0;
}

int main(void) {
    SceUID threads[KIND_COUNT * DELAY_COUNT];
    for (int i = 0; i < KIND_COUNT * DELAY_COUNT; i++) {
        threads[i] = test_start_thread_arg(delay_thread, i);
        TEST_WAIT_FOR(wait_type(threads[i]) != 0);
    }
    sceKernelDelayThread(300000);

    for (int kind = 0; kind < KIND_COUNT; kind++) {
        for (int v = 0; v < DELAY_COUNT; v++) {
            PRINTF(wait_type(threads[kind * DELAY_COUNT + v]),
                   "%s of 0x%X: its wait type after 300 ms", kind_names[kind], (unsigned)delays[v]);
        }
    }

    test_exit();
    return 0;
}
