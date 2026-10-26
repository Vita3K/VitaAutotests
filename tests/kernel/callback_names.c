// The name a callback keeps for each length, a name used twice, and the common pointer and the
// function pointer as they were given. The cases:
// - names of 0, 1, 31, 32, 33 and 255 characters, each callback made and then deleted. For each
//   one: the create result, whether the info name is the first 31 characters of the name given, and
//   whether byte 31 of the info name is 0
// - the same name given to two callbacks, both made, with different UIDs
// - a NULL common pointer and an odd one, each callback notified and run once. The info shows the
//   pointer it was given, and the run shows the fourth argument it got.
// - a function pointer with bit 0 clear, and one at 0x1000. Neither is ever notified, so only the
//   create result and the info function are printed.
// The names are the alphabet over and over, so a callback that kept the wrong characters shows. The
// empty name is also made by callback_errors.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

// The longest name given, and the name a callback can keep
#define LONGEST 255
#define KEPT 31

static char long_name[LONGEST + 1];
static void *volatile got_common;
static volatile int runs;
// Only its address is used, one byte past the start of it is an odd pointer
static int odd_anchor;

static const int lengths[] = { 0, 1, KEPT, 32, 33, LONGEST };

static int callback(int notify_id, int count, int arg, void *common) {
    (void)notify_id;
    (void)count;
    (void)arg;
    got_common = common;
    runs++;
    return 0;
}

// Notifies id once, runs its callbacks with a check, and returns the check's result
static int notify_and_check(SceUID id) {
    sceKernelNotifyCallback(id, 1);
    return sceKernelCheckCallback();
}

int main(void) {
    for (int i = 0; i < LONGEST; i++)
        long_name[i] = 'a' + i % 26;

    for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        const int len = lengths[i];
        char name[LONGEST + 1];
        sceClibMemcpy(name, long_name, len);
        name[len] = 0;
        const SceUID id = sceKernelCreateCallback(name, 0, callback, NULL);
        PRINTF(test_created(id), "a name of %d characters: create", len);
        if (id < 0)
            continue;
        CallbackInfo info = { .size = sizeof(info) };
        const int result = sceKernelGetCallbackInfo(id, (SceKernelCallbackInfo *)&info);
        PRINTF(result == 0 && sceClibStrncmp(info.name, name, KEPT) == 0,
               "a name of %d characters: the info name is the first 31 characters", len);
        PRINTF(info.name[KEPT] == 0, "a name of %d characters: byte 31 of the info name is 0", len);
        sceKernelDeleteCallback(id);
    }

    // Two callbacks with one name
    const SceUID twin_a = sceKernelCreateCallback("twin", 0, callback, NULL);
    const SceUID twin_b = sceKernelCreateCallback("twin", 0, callback, NULL);
    PRINT("the same name twice: the first create", test_created(twin_a));
    PRINT("the same name twice: the second create", test_created(twin_b));
    PRINT("the same name twice: the UIDs differ", twin_a != twin_b);
    if (twin_a >= 0)
        sceKernelDeleteCallback(twin_a);
    if (twin_b >= 0)
        sceKernelDeleteCallback(twin_b);

    // Common pointers, each callback run once after its own notify
    void *const odd = (char *)&odd_anchor + 1;
    const SceUID null_common = sceKernelCreateCallback("null common", 0, callback, NULL);
    const SceUID odd_common = sceKernelCreateCallback("odd common", 0, callback, odd);
    CallbackInfo info = { .size = sizeof(info) };
    info = (CallbackInfo){ .size = sizeof(info) };
    sceKernelGetCallbackInfo(null_common, (SceKernelCallbackInfo *)&info);
    PRINT("the NULL common in info", info.common == NULL);
    info = (CallbackInfo){ .size = sizeof(info) };
    sceKernelGetCallbackInfo(odd_common, (SceKernelCallbackInfo *)&info);
    PRINT("the odd common in info", info.common == odd);

    runs = 0;
    got_common = odd;
    PRINT("the NULL common check", notify_and_check(null_common));
    PRINT("the NULL common runs", runs);
    PRINT("the NULL common as the fourth argument", got_common == NULL);
    runs = 0;
    got_common = NULL;
    PRINT("the odd common check", notify_and_check(odd_common));
    PRINT("the odd common runs", runs);
    PRINT("the odd common as the fourth argument", got_common == odd);
    sceKernelDeleteCallback(null_common);
    sceKernelDeleteCallback(odd_common);

    // Function pointers, never notified. The first one is callback with bit 0 cleared.
    const SceKernelCallbackFunction clear_bit0 =
        (SceKernelCallbackFunction)((unsigned int)callback & ~1u);
    const SceKernelCallbackFunction low = (SceKernelCallbackFunction)0x1000;
    const SceUID even_fn = sceKernelCreateCallback("even function", 0, clear_bit0, NULL);
    PRINT("create with a function with bit 0 clear", test_created(even_fn));
    info = (CallbackInfo){ .size = sizeof(info) };
    sceKernelGetCallbackInfo(even_fn, (SceKernelCallbackInfo *)&info);
    PRINT("the info function with bit 0 clear is the function", info.callback == clear_bit0);
    const SceUID low_fn = sceKernelCreateCallback("low function", 0, low, NULL);
    PRINT("create with a function at 0x1000", test_created(low_fn));
    info = (CallbackInfo){ .size = sizeof(info) };
    sceKernelGetCallbackInfo(low_fn, (SceKernelCallbackInfo *)&info);
    PRINT("the info function at 0x1000 is the function", info.callback == low);
    sceKernelDeleteCallback(even_fn);
    sceKernelDeleteCallback(low_fn);

    test_exit();
    return 0;
}
