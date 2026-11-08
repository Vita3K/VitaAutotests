// What sceKernelExitDeleteThread from a callback that a delay with callbacks runs does, see callback_exit.h
#include "callback_exit.h"

int main(void) {
    exit_run(BY_DELAY, 1);
    test_exit();
    return 0;
}
