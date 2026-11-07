// A call to code whose first instruction is the permanently undefined UDF #0 in ARM state, with the
// harness of fetch_fault.h.
#include "fetch_fault.h"

__attribute__((naked, target("arm"))) static void undefined(void) {
    __asm__ volatile(".word 0xe7f000f0\n\tbx lr");
}

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    fetch_run(worker, (SceUInt32)undefined);
    test_exit();
    return 0;
}
