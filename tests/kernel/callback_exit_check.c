// What sceKernelExitThread from a callback that sceKernelCheckCallback runs does, see callback_exit.h
#include "callback_exit.h"

int main(void) {
    exit_run(BY_CHECK, 0);
    test_exit();
    return 0;
}
