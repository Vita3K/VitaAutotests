// A call to code whose first instruction is UDF.W #0, the permanently undefined 32 bit Thumb
// instruction, with the harness of fetch_fault.h.
#include "fetch_fault.h"

__attribute__((naked, target("thumb"))) static void undefined(void) {
    __asm__ volatile(".short 0xf7f0, 0xa000\n\tbx lr");
}

int main(void) {
    const SceUID worker = fetch_create_worker(fetch_worker);
    PRINT("create the worker", worker > 0);
    fetch_run(worker, (SceUInt32)undefined);
    test_exit();
    return 0;
}
