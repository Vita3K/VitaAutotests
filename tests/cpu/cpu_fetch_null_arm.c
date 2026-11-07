// A call to address 0 in ARM state, with the harness of fetch_fault.h.
//
// On a PS Vita TV on firmware 3.65 the process ends with a core dump when the worker makes the call,
// so the output stops at the release line.
#include "fetch_fault.h"

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    fetch_run(worker, 0);
    test_exit();
    return 0;
}
