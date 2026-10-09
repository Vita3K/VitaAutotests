// Lightweight mutex and condition variable calls write only inside their work areas
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define GUARD 0xA5A5A5A5u

typedef struct {
    unsigned int before[4];
    SceKernelLwMutexWork work;
    unsigned int after[4];
} GuardedMutex;

typedef struct {
    unsigned int before[4];
    SceKernelLwCondWork work;
    unsigned int after[4];
} GuardedCond;

static void fill(unsigned int *words, int count) {
    for (int i = 0; i < count; i++)
        words[i] = GUARD;
}

// Returns 1 if every guard word is untouched
static int intact(const unsigned int *before, const unsigned int *after) {
    for (int i = 0; i < 4; i++) {
        if (before[i] != GUARD || after[i] != GUARD)
            return 0;
    }
    return 1;
}

int main(void) {
    static GuardedMutex mutex;
    static GuardedCond cond;
    fill(mutex.before, 4);
    fill(mutex.after, 4);
    fill(cond.before, 4);
    fill(cond.after, 4);

    sceKernelCreateLwMutex(&mutex.work, "mutex", 0, 0, NULL);
    PRINT("create a lightweight mutex", intact(mutex.before, mutex.after));
    sceKernelLockLwMutex(&mutex.work, 1, NULL);
    PRINT("lock a lightweight mutex", intact(mutex.before, mutex.after));

    sceKernelCreateLwCond(&cond.work, "cond", 0, &mutex.work, NULL);
    PRINT("create a lightweight condition variable", intact(cond.before, cond.after));
    unsigned int timeout = 1000;
    sceKernelWaitLwCond(&cond.work, &timeout);
    PRINT("wait on a lightweight condition variable", intact(cond.before, cond.after));
    PRINT("the wait leaves the mutex work area alone", intact(mutex.before, mutex.after));
    sceKernelSignalLwCond(&cond.work);
    sceKernelDeleteLwCond(&cond.work);
    PRINT("delete a lightweight condition variable", intact(cond.before, cond.after));

    sceKernelUnlockLwMutex(&mutex.work, 1);
    sceKernelDeleteLwMutex(&mutex.work);
    PRINT("unlock and delete a lightweight mutex", intact(mutex.before, mutex.after));

    test_exit();
    return 0;
}
