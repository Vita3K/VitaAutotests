// What a wait ended by deleting its object leaves in its timeout
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID timer, sema;
static int wait_result;
static SceUInt32 timeout_left;

static int timer_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 result, timeout = TEST_WAIT_US;
    SceUInt64 user_data;
    wait_result = sceKernelWaitEvent(timer, TIMER_EVENT_BIT, &result, &user_data, &timeout);
    timeout_left = timeout;
    return 0;
}

static int sema_waiter(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt timeout = TEST_WAIT_US;
    wait_result = sceKernelWaitSema(sema, 1, &timeout);
    timeout_left = timeout;
    return 0;
}

int main(void) {
    timer = sceKernelCreateTimer("timer", 0, NULL);
    wait_result = -1;
    timeout_left = TEST_WAIT_US;
    SceUID waiter = test_start_thread(timer_waiter);
    test_wait_until_waiting(waiter);
    sceKernelDelayThread(100000);
    PRINT("delete a timer with a waiter", sceKernelDeleteTimer(timer));
    test_join_thread(waiter);
    PRINT("the timer wait", wait_result);
    PRINT("its timeout went down", timeout_left < TEST_WAIT_US);
    PRINT("its timeout is above 0", timeout_left > 0);

    sema = sceKernelCreateSema("sema", 0, 0, 1, NULL);
    wait_result = -1;
    timeout_left = TEST_WAIT_US;
    waiter = test_start_thread(sema_waiter);
    test_wait_until_waiting(waiter);
    sceKernelDelayThread(100000);
    PRINT("delete a semaphore with a waiter", sceKernelDeleteSema(sema));
    test_join_thread(waiter);
    PRINT("the semaphore wait", wait_result);
    PRINT("its timeout went down", timeout_left < TEST_WAIT_US);
    PRINT("its timeout is above 0", timeout_left > 0);

    test_exit();
    return 0;
}
