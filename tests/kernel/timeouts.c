// The time a wait writes back in each way it can end. Values are printed as whether they are 0,
// all of it, or in between, so they don't depend on timing.
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

#define GIVEN 200000

static SceUID sema;
static int wait_result;
static SceUInt time_left;

static int wait_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    time_left = GIVEN;
    wait_result = sceKernelWaitSema(sema, 1, &time_left);
    return 0;
}

// 0 if nothing is left, 1 if some is, 2 if all of it is
static int left(SceUInt value) {
    return value == 0 ? 0 : value >= GIVEN ? 2 : 1;
}

int main(void) {
    SceUInt timeout;
    sema = sceKernelCreateSema("sema", 0, 1, 4, NULL);

    timeout = GIVEN;
    PRINT("a wait that succeeds at once", sceKernelWaitSema(sema, 1, &timeout));
    PRINT("its time left", left(timeout));
    timeout = 1;
    PRINT("a wait of 1 microsecond", sceKernelWaitSema(sema, 1, &timeout));
    PRINT("its time left", left(timeout));
    timeout = 0xFFFFFFFF;
    sceKernelSignalSema(sema, 1);
    PRINT("a wait with the longest timeout that succeeds at once", sceKernelWaitSema(sema, 1, &timeout));
    PRINT("its time left is the same", timeout == 0xFFFFFFFF);

    SceUID thread = test_start_thread(wait_thread);
    test_wait_until_waiting(thread);
    sceKernelSignalSema(sema, 1);
    test_join_thread(thread);
    PRINT("a wait ended by a signal", wait_result);
    PRINT("its time left", left(time_left));

    thread = test_start_thread(wait_thread);
    test_wait_until_waiting(thread);
    int waiting;
    sceKernelCancelSema(sema, 0, &waiting);
    test_join_thread(thread);
    PRINT("a wait ended by a cancel", wait_result);
    PRINT("its time left", left(time_left));

    thread = test_start_thread(wait_thread);
    test_wait_until_waiting(thread);
    sceKernelDeleteSema(sema);
    test_join_thread(thread);
    PRINT("a wait ended by a delete", wait_result);
    PRINT("its time left", left(time_left));

    // The same for the other objects that time out
    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0, NULL);
    unsigned int bits;
    timeout = 1000;
    PRINT("an event flag wait that times out", sceKernelWaitEventFlag(flag, 1, EVF_WAITMODE_AND, &bits, &timeout));
    PRINT("its time left", left(timeout));
    const SceUID mutex = sceKernelCreateMutex("mutex", 0, 0, NULL);
    const SceUID cond = sceKernelCreateCond("cond", 0, mutex, NULL);
    sceKernelLockMutex(mutex, 1, NULL);
    timeout = 1000;
    PRINT("a condition wait that times out", sceKernelWaitCond(cond, &timeout));
    PRINT("its time left", left(timeout));
    sceKernelUnlockMutex(mutex, 1);
    const SceUID pipe = sceKernelCreateMsgPipe("pipe", MSG_PIPE_TYPE_USER, 0, 16, NULL);
    char buf[4];
    timeout = 1000;
    PRINT("a message pipe receive that times out", sceKernelReceiveMsgPipe(pipe, buf, 1, MSG_PIPE_MODE_FULL, NULL, &timeout));
    PRINT("its time left", left(timeout));

    test_exit();
    return 0;
}
