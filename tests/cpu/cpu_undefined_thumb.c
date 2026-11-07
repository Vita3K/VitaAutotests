// A call to code whose first instruction is the permanently undefined UDF #0 in Thumb state, with the
// harness of fetch_fault.h.
#include "fetch_fault.h"

__attribute__((naked, target("thumb"))) static void undefined(void) {
    __asm__ volatile(".short 0xde00\n\tbx lr");
}

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    fetch_run(worker, (SceUInt32)undefined);
    test_exit();
    return 0;
}
