// The process ends with the code given to sceKernelExitProcess. If the call returned, main would
// return 1 and the C runtime would exit again with it.
#include "test.h"

int main(void) {
    test_exit();
    return 1;
}
