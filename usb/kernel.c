// Kernel half of the USB link to run_tests.py. The shell plugin drives the USB serial port with the
// sceUsbSerial syscalls, but whatever USB mode the Vita is in has to let go of the port first, and
// only the kernel can make it. This plugin gives the shell plugin a syscall for that, and turns MTP
// off, which would take the port back when a cable is connected.
//
// The firmware exports no kernel USB serial functions, and SceUdcd is looked up when it's first
// needed, so this plugin has no static import that could keep it from loading at boot.
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <taihen.h>

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

#define LOG_PATH "ur0:tai/vita-autotests-kernel.log"

#define SCE_UDCD_FOR_DRIVER 0xBC05A8FB
#define KSCE_UDCD_STOP_CURRENT_INTERNAL 0x0F3595AE
// The bus of the USB port
#define UDCD_BUS 2

// Returned when ksceUdcdStopCurrentInternal can't be found
#define NOT_RESOLVED ((int)0x8E000001)

static int (*udcd_stop_current)(int bus);

// Starts MTP, which the shell does when a USB cable is connected, taking the port from the USB
// serial port. Like PSMLogUSB, the hook turns MTP off for good: it never calls the original, whose
// arguments aren't known.
#define SCE_USB_MTP_START 0x6FC97594

static SceUID mtp_hook = -1;
static tai_hook_ref_t mtp_ref;

// Appends "<label>: 0x<value>" to LOG_PATH, which says how the link came up, since this boot
static void log_value(const char *label, int value) {
    char line[80];
    int len = 0;
    while (*label != '\0' && len < 60)
        line[len++] = *label++;
    line[len++] = ':';
    line[len++] = ' ';
    line[len++] = '0';
    line[len++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4)
        line[len++] = "0123456789ABCDEF"[((unsigned)value >> shift) & 0xF];
    line[len++] = '\n';
    const SceUID fd = ksceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        ksceIoWrite(fd, line, len);
        ksceIoClose(fd);
    }
}

// Keeps MTP from taking the port. It doesn't log, since file I/O may not be safe wherever MTP is
// started from.
static int mtp_start_patched(void) {
    return 0;
}

// Hooks the start of MTP, if it isn't hooked yet and SceUsbMtp is loaded
static void hook_mtp(void) {
    if (mtp_hook >= 0)
        return;
    mtp_hook = taiHookFunctionExportForKernel(KERNEL_PID, &mtp_ref, "SceUsbMtp", TAI_ANY_LIBRARY, SCE_USB_MTP_START, mtp_start_patched);
    log_value("hook the MTP start", mtp_hook);
}

// Stops the USB mode that has the port, like MTP, so the USB serial port can take it. Returns the
// result of ksceUdcdStopCurrentInternal.
int vitaAutotestsUsbStopUdc(void) {
    int state;
    ENTER_SYSCALL(state);
    hook_mtp();
    int ret = NOT_RESOLVED;
    if (udcd_stop_current == NULL) {
        uintptr_t func = 0;
        const int found = module_get_export_func(KERNEL_PID, "SceUdcd", SCE_UDCD_FOR_DRIVER, KSCE_UDCD_STOP_CURRENT_INTERNAL, &func);
        log_value("find ksceUdcdStopCurrentInternal", found);
        if (found >= 0)
            udcd_stop_current = (void *)func;
    }
    if (udcd_stop_current != NULL)
        ret = udcd_stop_current(UDCD_BUS);
    log_value("ksceUdcdStopCurrentInternal", ret);
    EXIT_SYSCALL(state);
    return ret;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    ksceIoRemove(LOG_PATH);
    log_value("kernel plugin started", 1);
    hook_mtp();
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (mtp_hook >= 0)
        taiHookReleaseForKernel(mtp_hook, mtp_ref);
    return SCE_KERNEL_STOP_SUCCESS;
}
