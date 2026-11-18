// A store to the base of a block that was freed, in a worker, with the harness of fetch_fault.h.
#include "data_abort.h"

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    data_abort_address = data_abort_freed_base();
    fetch_run(worker, (SceUInt32)data_abort_store);
    test_exit();
    return 0;
}
