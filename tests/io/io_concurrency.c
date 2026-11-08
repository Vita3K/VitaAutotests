// Concurrent I/O: operations while closing, and calls on closed descriptors
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

#include "test.h"

#define TEST_FILE "ux0:data/vita-autotests/io/concurrent.bin"

static volatile int stop_threads = 0;
static SceUID shared_fd = -1;
static volatile int thread_write_count = 0;
static volatile int thread_read_count = 0;

static int writer_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    char buf[16] = "0123456789abcde";
    while (!stop_threads) {
        int ret = sceIoWrite(shared_fd, buf, sizeof(buf));
        if (ret > 0)
            thread_write_count++;
        else if (ret < 0)
            break;
    }
    return 0;
}

static int reader_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    char buf[16];
    while (!stop_threads) {
        int ret = sceIoRead(shared_fd, buf, sizeof(buf));
        if (ret > 0)
            thread_read_count++;
        else if (ret < 0)
            break;
    }
    return 0;
}

int main(void) {
    sceIoMkdir("ux0:data/vita-autotests/io", 0777);
    sceIoRemove(TEST_FILE);

    shared_fd = sceIoOpen(TEST_FILE, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    PRINT("open shared fd", test_created(shared_fd));

    char init_buf[64];
    sceClibMemset(init_buf, 'A', sizeof(init_buf));
    sceIoWrite(shared_fd, init_buf, sizeof(init_buf));
    sceIoLseek(shared_fd, 0, SCE_SEEK_SET);

    SceUID th_writer = sceKernelCreateThread("writer", writer_thread, 0x10000100, 0x10000, 0, 0, NULL);
    SceUID th_reader = sceKernelCreateThread("reader", reader_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(th_writer, 0, NULL);
    sceKernelStartThread(th_reader, 0, NULL);

    sceKernelDelayThread(50000);

    int close_ret = sceIoClose(shared_fd);
    PRINT("concurrent close ret", close_ret);

    stop_threads = 1;
    sceKernelWaitThreadEnd(th_writer, NULL, NULL);
    sceKernelWaitThreadEnd(th_reader, NULL, NULL);

    PRINT("writer executed ops", thread_write_count > 0);
    PRINT("reader executed ops", thread_read_count > 0);

    char dummy[16];
    PRINT("read after close", sceIoRead(shared_fd, dummy, sizeof(dummy)));
    PRINT("write after close", sceIoWrite(shared_fd, dummy, sizeof(dummy)));
    PRINT("seek after close", (int)sceIoLseek(shared_fd, 0, SCE_SEEK_SET));

    sceIoRemove(TEST_FILE);
    test_exit();
    return 0;
}
