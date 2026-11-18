// A THREAD_ATTR_VM worker runs the undefined instruction UDF #0, which no thread can catch.
#include "vm_fault.h"

int main(void) {
    vm_fault_run(vm_worker_undefined);
    test_exit();
    return 0;
}
