// What the lightweight mutex and condition variable work areas hold after each call, and using
// a copied or deleted work area. Games can look inside these, so what they hold is part of the
// behavior. Words that hold the calling thread's UID print as 0x7EAD0001 and UIDs of other objects
// as 0x7EA00000 plus their class, so the output doesn't depend on UID values.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static int normalize(unsigned int word) {
    if (word == (unsigned int)sceKernelGetThreadId())
        return 0x7EAD0001;
    // Small values are counts and attributes, not UIDs
    const SceInt32 uid_class = word > 0x10 ? get_uid_class(word) : -1;
    if (uid_class > 0)
        return 0x7EA00000 | uid_class;
    return word;
}

static void print_words(const char *name, const void *work, int words) {
    const unsigned int *w = work;
    char label[64];
    for (int i = 0; i < words; i++) {
        sceClibSnprintf(label, sizeof(label), "%s word %d", name, i);
        PRINT(label, normalize(w[i]));
    }
}

int main(void) {
    static SceKernelLwMutexWork mutex;
    sceClibMemset(&mutex, 0xEE, sizeof(mutex));
    sceKernelCreateLwMutex(&mutex, "mutex", MUTEX_ATTR_RECURSIVE, 0, NULL);
    print_words("created", &mutex, 8);
    sceKernelLockLwMutex(&mutex, 1, NULL);
    print_words("locked once", &mutex, 8);
    sceKernelLockLwMutex(&mutex, 2, NULL);
    print_words("locked three times", &mutex, 8);
    sceKernelUnlockLwMutex(&mutex, 3);
    print_words("unlocked", &mutex, 8);

    static SceKernelLwCondWork cond;
    sceClibMemset(&cond, 0xEE, sizeof(cond));
    sceKernelCreateLwCond(&cond, "cond", 0, &mutex, NULL);
    print_words("condition created", &cond, 4);

    // A copy of the work area somewhere else in memory
    static SceKernelLwMutexWork copy;
    sceClibMemcpy(&copy, &mutex, sizeof(mutex));
    PRINT("lock the copy", sceKernelTryLockLwMutex(&copy, 1));
    PRINT("unlock the copy", sceKernelUnlockLwMutex(&copy, 1));

    sceKernelDeleteLwCond(&cond);
    print_words("condition deleted", &cond, 4);
    sceKernelDeleteLwMutex(&mutex);
    print_words("deleted", &mutex, 8);
    PRINT("lock a deleted lightweight mutex", sceKernelTryLockLwMutex(&mutex, 1));
    PRINT("signal a deleted lightweight condition", sceKernelSignalLwCond(&cond));

    test_exit();
    return 0;
}
