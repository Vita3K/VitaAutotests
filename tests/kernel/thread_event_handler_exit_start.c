// What sceKernelExitThread from a START handler does, see thread_event_exit.h
#include "thread_event_exit.h"

int main(void) {
    exit_run(SCE_KERNEL_THREAD_EVENT_TYPE_START, 0);
    test_exit();
    return 0;
}
