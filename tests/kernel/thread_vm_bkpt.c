// A THREAD_ATTR_VM worker runs BKPT #0. The thread stops and the main thread gets the record.
#include "vm_fault.h"

int main(void) {
    vm_fault_run(vm_worker_bkpt);
    test_exit();
    return 0;
}
