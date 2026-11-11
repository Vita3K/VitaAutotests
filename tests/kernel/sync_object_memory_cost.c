// How much main memory kernel objects take. The main free size is measured to 4 KiB with 4 KiB
// USER_RW probes, as app_free_sizes does. The cases:
// - 0x400 semaphores created, then deleted, then 0x400 created and deleted again: the KiB taken by
//   the first creates, the KiB still taken after the deletes, and the KiB the second creates take
// - 0x100 lightweight mutexes created and deleted, twice: the KiB taken by the first round, and
//   by the second
// - 0x40 threads with a stack of 0x4000 made and never started, then deleted: the KiB taken, and
//   the KiB still taken after the deletes
// Everything is measured first and printed after, so the output file doesn't take memory in
// between.
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "test.h"

#define KIB 0x400
#define PROBES 320
#define SEMAS 0x400
#define MUTEXES 0x100
#define THREADS 0x40

static SceUID probes[PROBES];
static SceUID semas[SEMAS];
static SceKernelLwMutexWork mutex_works[MUTEXES] __attribute__((aligned(8)));
static SceUID threads[THREADS];

static int reported(void) {
    SceKernelFreeMemorySizeInfo sizes = { .size = sizeof(sizes) };
    sceKernelGetFreeMemorySize(&sizes);
    return sizes.size_user;
}

static int measure(void) {
    const int start = reported();
    int made = 0, now = start;
    while (made < PROBES && now == start) {
        const SceUID block = sceKernelAllocMemBlock("free probe", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4 * KIB, NULL);
        if (block < 0)
            break;
        probes[made++] = block;
        now = reported();
    }
    for (int i = 0; i < made; i++)
        sceKernelFreeMemBlock(probes[i]);
    return now == start ? -1 : start + (made - 1) * 4 * KIB;
}

static int idle_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

// Creates and deletes the semaphores, and returns the free size after the creates
static int sema_round(int *created) {
    int made = 0;
    for (int i = 0; i < SEMAS; i++) {
        semas[i] = sceKernelCreateSema("cost", 0, 0, 1, NULL);
        made += semas[i] >= 0;
    }
    *created = made;
    const int with = measure();
    for (int i = 0; i < SEMAS; i++)
        sceKernelDeleteSema(semas[i]);
    return with;
}

static int mutex_round(int *created) {
    int made = 0;
    SceUID ids[MUTEXES];
    for (int i = 0; i < MUTEXES; i++) {
        ids[i] = sceKernelCreateLwMutex(&mutex_works[i], "cost", 0, 0, NULL);
        made += ids[i] >= 0;
    }
    *created = made;
    const int with = measure();
    for (int i = 0; i < MUTEXES; i++)
        sceKernelDeleteLwMutex(&mutex_works[i]);
    return with;
}

int main(void) {
    measure();
    const SceUID warm = sceKernelCreateSema("warm", 0, 0, 1, NULL);
    sceKernelDeleteSema(warm);

    int sema_made1, sema_made2, mutex_made1, mutex_made2, thread_made = 0;
    const int sema_start = measure();
    const int sema_with1 = sema_round(&sema_made1);
    const int sema_after1 = measure();
    const int sema_with2 = sema_round(&sema_made2);
    const int sema_after2 = measure();

    const int mutex_start = measure();
    const int mutex_with1 = mutex_round(&mutex_made1);
    const int mutex_after1 = measure();
    const int mutex_with2 = mutex_round(&mutex_made2);
    const int mutex_after2 = measure();

    const int thread_start = measure();
    for (int i = 0; i < THREADS; i++) {
        threads[i] = sceKernelCreateThread("cost", idle_entry, 0x60, 0x4000, 0, 0, NULL);
        thread_made += threads[i] >= 0;
    }
    const int thread_with = measure();
    for (int i = 0; i < THREADS; i++)
        sceKernelDeleteThread(threads[i]);
    const int thread_after = measure();

    PRINT("semaphores, first round: all were created", sema_made1 == SEMAS);
    PRINT("semaphores, first round: the KiB taken by the creates", (sema_start - sema_with1) / KIB);
    PRINT("semaphores, first round: the KiB still taken after the deletes", (sema_start - sema_after1) / KIB);
    PRINT("semaphores, second round: all were created", sema_made2 == SEMAS);
    PRINT("semaphores, second round: the KiB taken by the creates, from the free size after the first round", (sema_after1 - sema_with2) / KIB);
    PRINT("semaphores, second round: the KiB still taken after the deletes, in all", (sema_start - sema_after2) / KIB);
    PRINT("lightweight mutexes, first round: all were created", mutex_made1 == MUTEXES);
    PRINT("lightweight mutexes, first round: the KiB taken by the creates", (mutex_start - mutex_with1) / KIB);
    PRINT("lightweight mutexes, first round: the KiB still taken after the deletes", (mutex_start - mutex_after1) / KIB);
    PRINT("lightweight mutexes, second round: all were created", mutex_made2 == MUTEXES);
    PRINT("lightweight mutexes, second round: the KiB taken by the creates, from the free size after the first round", (mutex_after1 - mutex_with2) / KIB);
    PRINT("lightweight mutexes, second round: the KiB still taken after the deletes, in all", (mutex_start - mutex_after2) / KIB);
    PRINT("threads: all were created", thread_made == THREADS);
    PRINT("threads: the KiB taken by the creates", (thread_start - thread_with) / KIB);
    PRINT("threads: the KiB still taken after the deletes", (thread_start - thread_after) / KIB);
    test_exit();
    return 0;
}
