// The display: refresh rate, the largest framebuffer, the vblank count, and which framebuffers
// sceDisplaySetFrameBuf takes. A valid framebuffer is a black buffer in CDRAM.
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>

#include "test.h"

#define WIDTH 960
#define HEIGHT 544
#define BUF_SIZE 0x200000

static void *buf;

// Sets a framebuffer like the valid one but with one field changed, and prints the result
static void set_with(const char *what, SceDisplayFrameBuf fb) {
    PRINTF(sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME), "set a framebuffer with %s", what);
}

int main(void) {
    float fps = 0;
    PRINT("get the refresh rate", sceDisplayGetRefreshRate(&fps));
    PRINT("it in hundredths of a hertz", (int)(fps * 100 + 0.5f));
    int width = 0, height = 0;
    PRINT("get the largest framebuffer", sceDisplayGetMaximumFrameBufResolution(&width, &height));
    PRINT("its width", width);
    PRINT("its height", height);
    PRINT("the primary head", sceDisplayGetPrimaryHead());

    // The vblank count
    const int before = sceDisplayGetVcount();
    PRINT("wait for a vblank", sceDisplayWaitVblankStart());
    PRINT("the count goes up", sceDisplayGetVcount() > before);
    const int before3 = sceDisplayGetVcount();
    PRINT("wait for 3 vblanks", sceDisplayWaitVblankStartMulti(3));
    PRINT("the count goes up by at least 2", sceDisplayGetVcount() - before3 >= 2);

    // Framebuffers
    const SceUID block = sceKernelAllocMemBlock("fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, BUF_SIZE, NULL);
    sceKernelGetMemBlockBase(block, &buf);
    sceClibMemset(buf, 0, BUF_SIZE);
    const SceDisplayFrameBuf valid = { sizeof(SceDisplayFrameBuf), buf, WIDTH, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, WIDTH, HEIGHT };
    SceDisplayFrameBuf fb = valid;
    PRINT("set a framebuffer", sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME));
    PRINT("wait for it to take effect", sceDisplayWaitSetFrameBuf());
    SceDisplayFrameBuf got = { .size = sizeof(got) };
    PRINT("get the framebuffer", sceDisplayGetFrameBuf(&got, SCE_DISPLAY_SETBUF_NEXTFRAME));
    PRINT("it is the one set", got.base == buf && got.pitch == WIDTH && got.width == WIDTH && got.height == HEIGHT && got.pixelformat == 0);
    fb = valid;
    PRINT("set it immediately", sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE));
    fb = valid;
    PRINT("set it with sync mode 2", sceDisplaySetFrameBuf(&fb, 2));

    SceDisplayFrameBuf changed = valid;
    changed.size = 0;
    set_with("size 0", changed);
    changed = valid;
    changed.pitch = WIDTH - 1;
    set_with("a pitch under its width", changed);
    changed = valid;
    changed.pitch = 1024;
    set_with("pitch 1024", changed);
    changed = valid;
    changed.pitch = 1000;
    set_with("pitch 1000", changed);
    changed = valid;
    changed.width = 480;
    changed.height = 272;
    set_with("480 by 272", changed);
    changed = valid;
    changed.width = 640;
    changed.height = 368;
    set_with("640 by 368", changed);
    changed = valid;
    changed.width = 720;
    changed.height = 408;
    set_with("720 by 408", changed);
    changed = valid;
    changed.width = 800;
    changed.height = 450;
    set_with("800 by 450", changed);
    changed = valid;
    changed.width = 0;
    set_with("width 0", changed);
    changed = valid;
    changed.height = 1088;
    set_with("height 1088", changed);
    changed = valid;
    changed.pixelformat = SCE_DISPLAY_PIXELFORMAT_A2B10G10R10;
    set_with("the 10 bit format", changed);
    changed = valid;
    changed.pixelformat = 0x1;
    set_with("format 1", changed);
    changed = valid;
    changed.base = (char *)buf + 4;
    set_with("a base 4 bytes in", changed);
    changed = valid;
    changed.base = (char *)buf + 256;
    set_with("a base 256 bytes in", changed);
    changed = valid;
    changed.base = &fb;
    set_with("a base on the stack", changed);

    // NULL pointers last, so an implementation that crashes on them loses nothing else
    PRINT("get the refresh rate into a NULL pointer", sceDisplayGetRefreshRate(NULL));
    changed = valid;
    changed.base = NULL;
    set_with("a NULL base", changed);
    PRINT("set no framebuffer", sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME));
    PRINT("get the framebuffer into a NULL pointer", sceDisplayGetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME));

    test_exit();
    return 0;
}
