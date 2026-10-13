// Shell half of the USB link to run_tests.py. Loaded into SceShell, it opens the Vita's USB serial
// port, once vita-autotests-usb.skprx made the USB mode the Vita was in let go of it, reads
// commands from it, and runs each test by launching the test app.
//
// The host sends one command per line:
// - "hello", answered with "vita-autotests usb 1".
// - "run <name> <timeout in ms>", which runs the test <name> in a fresh process and answers with
//   "output <length>", the bytes of its output file, then "end <status>". The status is "exited" when
//   the app ended by itself, "timeout" when it was closed after the timeout, or "not-started".
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/usbserial.h>

#include "usb.h"

#define TITLE_ID "VTST00000"
#define OUTPUT_DIR "ux0:data/vita-autotests/output"
// On ur0:, since this plugin loads at boot, before ux0: may be there
#define LOG_PATH "ur0:tai/vita-autotests-usb.log"

#define POLL_US 10000
// How long the test app has to start after a launch before it's launched again, how many times,
// and how long it has to close after it's destroyed
#define LAUNCH_WAIT_US 3000000
#define LAUNCH_TRIES 5
#define CLOSE_TIMEOUT_US 5000000

static char chunk[0x1000];

// Appends a line to LOG_PATH, which says how the link came up, since this boot
static void log_line(const char *fmt, int value) {
    char line[96];
    const int len = sceClibSnprintf(line, sizeof(line), fmt, value);
    const SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1);
        sceIoClose(fd);
    }
}

// sceUsbSerialStatus before sceUsbSerialStart, and after the port broke, like when another USB
// mode took over
#define SERIAL_NOT_STARTED ((int)0x80244401)
#define SERIAL_BROKEN ((int)0x80244403)

// Starts the USB serial port if it isn't running. Returns 1 when a host has it open, 0 while it
// waits for one, or an error.
static int usb_connect(void) {
    int status = sceUsbSerialStatus();
    if (status == SERIAL_BROKEN) {
        log_line("close a broken port: 0x%08X\n", sceUsbSerialClose());
        status = SERIAL_NOT_STARTED;
    }
    if (status == SERIAL_NOT_STARTED) {
        // Whatever USB mode the Vita is in, like MTP, has the port and must let go of it first
        log_line("stop the USB mode: 0x%08X\n", vitaAutotestsUsbStopUdc());
        status = sceUsbSerialStart();
        log_line("sceUsbSerialStart: 0x%08X\n", status);
        if (status >= 0) {
            status = sceUsbSerialSetup(1);
            log_line("sceUsbSerialSetup: 0x%08X\n", status);
        }
        if (status >= 0)
            status = sceUsbSerialStatus();
    }
    return status;
}

// Sends len bytes to the host. Returns 0, or an error once the host is gone.
static int send(const void *data, int len) {
    const char *p = data;
    while (len > 0) {
        const int size = len < 0x1000 ? len : 0x1000;
        const int ret = (int)sceUsbSerialSend(p, size, 0, -1);
        if (ret < 0)
            return ret;
        p += size;
        len -= size;
    }
    return 0;
}

static int send_str(const char *str) {
    return send(str, sceClibStrnlen(str, 0x1000));
}

// Reads a line from the host into buf, without its line end. Returns its length, or an error once
// the host is gone.
static int recv_line(char *buf, int size) {
    int len = 0;
    for (;;) {
        char c;
        const int got = (int)sceUsbSerialRecv(&c, 1, 0, -1);
        if (got < 0)
            return got;
        if (got == 0) {
            sceKernelDelayThread(POLL_US);
            continue;
        }
        if (c == '\n') {
            buf[len] = '\0';
            return len;
        }
        if (c != '\r' && len < size - 1)
            buf[len++] = c;
    }
}

static int app_running(void) {
    SceUID pid;
    return sceAppMgrGetIdByName(&pid, TITLE_ID) >= 0;
}

static int file_exists(const char *path) {
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

// Waits until the test app is running, or has written its output file, for at most timeout_us.
// Returns 0 if it didn't.
static int wait_started(const char *output_path, long long timeout_us) {
    for (long long waited = 0; waited < timeout_us; waited += POLL_US) {
        if (app_running() || file_exists(output_path))
            return 1;
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

// Waits until the test app isn't running, for at most timeout_us. Returns 0 if it still is.
static int wait_ended(long long timeout_us) {
    for (long long waited = 0; waited < timeout_us; waited += POLL_US) {
        if (!app_running())
            return 1;
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

static void close_app(void) {
    sceAppMgrDestroyOtherApp();
    wait_ended(CLOSE_TIMEOUT_US);
}

// Sends "output <length>" and the bytes of the file at path, or a length of 0 if there is none
static int send_file(const char *path) {
    const SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    int size = 0;
    if (fd >= 0) {
        size = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);
        if (size < 0)
            size = 0;
    }
    char header[32];
    sceClibSnprintf(header, sizeof(header), "output %d\n", size);
    int ret = send_str(header);
    for (int left = size; ret >= 0 && left > 0;) {
        const int want = left < (int)sizeof(chunk) ? left : (int)sizeof(chunk);
        int got = sceIoRead(fd, chunk, want);
        // Keeps to the promised length even if the read fails
        if (got <= 0) {
            sceClibMemset(chunk, 0, want);
            got = want;
        }
        ret = send(chunk, got);
        left -= got;
    }
    if (fd >= 0)
        sceIoClose(fd);
    return ret;
}

static int run_test(const char *name, int timeout_ms) {
    char output_path[128];
    char uri[128];
    sceClibSnprintf(output_path, sizeof(output_path), OUTPUT_DIR "/%s.txt", name);
    sceClibSnprintf(uri, sizeof(uri), "psgm:play?titleid=" TITLE_ID "&arg=%s", name);

    // A launch would only resume a test app left from an earlier run
    if (app_running())
        close_app();
    sceIoRemove(output_path);

    // A launch right after the app of the test before ended is ignored, so a launch that doesn't
    // start the app is tried again
    int started = 0;
    for (int i = 0; i < LAUNCH_TRIES && !started; i++) {
        // Like VitaShell, launches twice, the first launch may only bring the app to the front
        sceAppMgrLaunchAppByUri(0xFFFFF, uri);
        sceKernelDelayThread(10000);
        sceAppMgrLaunchAppByUri(0xFFFFF, uri);
        started = wait_started(output_path, LAUNCH_WAIT_US);
    }

    const char *status = "exited";
    if (!started) {
        status = "not-started";
    } else if (!wait_ended((long long)timeout_ms * 1000)) {
        status = "timeout";
        close_app();
    }

    int ret = send_file(output_path);
    if (ret >= 0)
        ret = send_str("end ");
    if (ret >= 0)
        ret = send_str(status);
    if (ret >= 0)
        ret = send_str("\n");
    return ret;
}

// Parses a decimal number, returns def if str doesn't start with one
static int parse_int(const char *str, int def) {
    if (*str < '0' || *str > '9')
        return def;
    int value = 0;
    while (*str >= '0' && *str <= '9')
        value = value * 10 + (*str++ - '0');
    return value;
}

static int handle(char *line) {
    if (sceClibStrcmp(line, "hello") == 0)
        return send_str("vita-autotests usb 1\n");
    if (sceClibStrncmp(line, "run ", 4) == 0) {
        char *name = line + 4;
        char *end = name;
        while (*end != '\0' && *end != ' ')
            end++;
        const int timeout_ms = parse_int(*end == ' ' ? end + 1 : end, 60000);
        *end = '\0';
        return run_test(name, timeout_ms);
    }
    return send_str("unknown command\n");
}

static int link_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int last_status = 0x7FFFFFFF;
    for (;;) {
        const int status = usb_connect();
        if (status != last_status) {
            log_line("status: 0x%08X\n", status);
            last_status = status;
        }
        if (status != 1) {
            sceKernelDelayThread(100000);
            continue;
        }
        char line[128];
        const int len = recv_line(line, sizeof(line));
        if (len >= 0)
            handle(line);
        else
            log_line("recv: 0x%08X\n", len);
    }
    return 0;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sceIoRemove(LOG_PATH);
    log_line("loaded: %d\n", 1);
    const SceUID thread = sceKernelCreateThread("vita_autotests_usb", link_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (thread < 0)
        return SCE_KERNEL_START_FAILED;
    sceKernelStartThread(thread, 0, NULL);
    return SCE_KERNEL_START_SUCCESS;
}

// The thread waits on the host and can't be stopped, so the plugin stays loaded
int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return SCE_KERNEL_STOP_CANCEL;
}
