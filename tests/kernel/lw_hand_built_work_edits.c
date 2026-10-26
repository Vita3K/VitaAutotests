// Edits to the work area of a live lightweight mutex, made while the mutex is held. The kernel
// reads the owner, count and attribute words of the area on each call, and the info by work area
// call reads the same words back, so each case edits one word and then checks what a call and the
// info make of it. The cases:
// - the count word set to 5 on a recursive mutex held once: the info count, then an unlock of 5
// - the owner word set to the UID of another thread: an unlock by the caller, then the info owner,
//   then the owner word put back and an unlock
// - the owner word set to 0x12345, which names no thread: the info owner, then the owner word put
//   back and an unlock
// - the attribute word cleared on a recursive mutex held once: a lock of 1, which uses the
//   object's own attribute (recursive) and not work word 3, so it succeeds and the count grows,
//   then the words, and the info attribute and count
// - the attribute word set to recursive on a plain mutex held once: a lock of 1, the info count and
//   attribute, and two unlocks
// Another thread waits on a semaphore for the whole test, so its UID is that of a live thread.
// lw_hand_built_work_areas covers areas that name other objects. This covers the area of a mutex.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"
#include "threadmgr_extra.h"

static SceUID gate;
// The user UID of the parked thread
static volatile unsigned int other_id;

// Records its UID and waits on the gate, so it stays parked for the whole test
static int parked_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    SceUInt32 timeout = 3 * TEST_WAIT_US;
    other_id = (unsigned int)sceKernelGetThreadId();
    sceKernelWaitSema(gate, 1, &timeout);
    return 0;
}

// Writes word index of a work area, as the program would
static void set_word(SceKernelLwMutexWork *work, int index, unsigned int value) {
    ((volatile unsigned int *)work)[index] = value;
}

// Reads word index of a work area
static unsigned int get_word(const SceKernelLwMutexWork *work, int index) {
    return ((const volatile unsigned int *)work)[index];
}

int main(void) {
    gate = sceKernelCreateSema("gate", 0, 0, 1, NULL);
    const SceUID parked = test_start_thread(parked_entry);
    TEST_WAIT_FOR(test_in_wait(parked, WAIT_TYPE_SEMA, gate));
    const unsigned int self = (unsigned int)sceKernelGetThreadId();
    SceUInt32 timeout = 0;
    LwMutexInfo info;

    // The count word of a recursive mutex, changed to 5 while held once
    static SceKernelLwMutexWork count;
    PRINT("create the count mutex",
          sceKernelCreateLwMutex(&count, "count", LW_MUTEX_ATTR_RECURSIVE, 0, NULL));
    sceKernelLockLwMutex(&count, 1, NULL);
    set_word(&count, 2, 5);
    info = (LwMutexInfo){ .size = sizeof(info) };
    PRINT("info by work area after the count edit", sceKernelGetLwMutexInfo(&count, &info));
    PRINT("info count after the count edit is 5", info.currentCount == 5);
    PRINT("unlock of 5 after the count edit", sceKernelUnlockLwMutex(&count, 5));
    PRINT("words 0 and 2 are 0 after that unlock",
          get_word(&count, 0) == 0 && get_word(&count, 2) == 0);
    PRINT("delete the count mutex", sceKernelDeleteLwMutex(&count));

    // The owner word of a plain mutex, changed to the UID of another thread while held once
    static SceKernelLwMutexWork owner;
    PRINT("create the owner mutex", sceKernelCreateLwMutex(&owner, "owner", 0, 0, NULL));
    sceKernelLockLwMutex(&owner, 1, NULL);
    set_word(&owner, 0, other_id);
    PRINT("unlock by the caller with another thread in the owner word",
          sceKernelUnlockLwMutex(&owner, 1));
    info = (LwMutexInfo){ .size = sizeof(info) };
    PRINT("info by work area after the owner edit", sceKernelGetLwMutexInfo(&owner, &info));
    PRINT("info owner is the other thread", info.currentOwnerId == (SceUID)other_id);
    set_word(&owner, 0, self);
    PRINT("unlock by the caller after the owner word is put back",
          sceKernelUnlockLwMutex(&owner, 1));
    PRINT("delete the owner mutex", sceKernelDeleteLwMutex(&owner));

    // The owner word of a plain mutex set to a UID that names no thread
    static SceKernelLwMutexWork raw;
    PRINT("create the raw owner mutex", sceKernelCreateLwMutex(&raw, "raw", 0, 0, NULL));
    sceKernelLockLwMutex(&raw, 1, NULL);
    set_word(&raw, 0, 0x12345);
    info = (LwMutexInfo){ .size = sizeof(info) };
    PRINT("info by work area after the raw owner edit", sceKernelGetLwMutexInfo(&raw, &info));
    PRINT("info owner is the raw word 0x12345", info.currentOwnerId == 0x12345);
    set_word(&raw, 0, self);
    PRINT("unlock by the caller after the raw owner word is put back",
          sceKernelUnlockLwMutex(&raw, 1));
    PRINT("delete the raw owner mutex", sceKernelDeleteLwMutex(&raw));

    // The attribute word of a recursive mutex cleared while held once, so the words say plain
    static SceKernelLwMutexWork cleared;
    PRINT("create the cleared mutex",
          sceKernelCreateLwMutex(&cleared, "cleared", LW_MUTEX_ATTR_RECURSIVE, 0, NULL));
    sceKernelLockLwMutex(&cleared, 1, NULL);
    set_word(&cleared, 3, 0);
    timeout = 0;
    PRINT("lock of 1 after the attribute word is cleared",
          sceKernelLockLwMutex(&cleared, 1, &timeout));
    PRINT("words 0 to 2 are unchanged by that lock",
          get_word(&cleared, 0) == self && get_word(&cleared, 1) == 0 &&
              get_word(&cleared, 2) == 1);
    info = (LwMutexInfo){ .size = sizeof(info) };
    PRINT("info by work area after the attribute edit", sceKernelGetLwMutexInfo(&cleared, &info));
    PRINT("info attribute is still recursive", info.attr == LW_MUTEX_ATTR_RECURSIVE);
    PRINT("info count is still 1", info.currentCount == 1);
    PRINT("unlock of 1 after the attribute edit", sceKernelUnlockLwMutex(&cleared, 1));
    PRINT("delete the cleared mutex", sceKernelDeleteLwMutex(&cleared));

    // The attribute word of a plain mutex set to recursive while held once, so the words say
    // recursive
    static SceKernelLwMutexWork recursive;
    PRINT("create the plain mutex", sceKernelCreateLwMutex(&recursive, "plain", 0, 0, NULL));
    sceKernelLockLwMutex(&recursive, 1, NULL);
    set_word(&recursive, 3, LW_MUTEX_ATTR_RECURSIVE);
    timeout = 0;
    PRINT("lock of 1 after the attribute word is set to recursive",
          sceKernelLockLwMutex(&recursive, 1, &timeout));
    info = (LwMutexInfo){ .size = sizeof(info) };
    PRINT("info by work area after the attribute edit to recursive",
          sceKernelGetLwMutexInfo(&recursive, &info));
    PRINT("info count is 2", info.currentCount == 2);
    PRINT("info attribute is still plain", info.attr == 0);
    PRINT("first unlock of 1", sceKernelUnlockLwMutex(&recursive, 1));
    PRINT("second unlock of 1", sceKernelUnlockLwMutex(&recursive, 1));
    PRINT("delete the plain mutex", sceKernelDeleteLwMutex(&recursive));

    sceKernelSignalSema(gate, 1);
    test_join_thread(parked);
    sceKernelDeleteSema(gate);
    test_exit();
    return 0;
}
