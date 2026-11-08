// FIOS2 path normalization: dot, dot-dot, slashes and backslashes in overlays
#include "fios.h"

#define P FIOS_DIR "/pnorm"

static char out[0x400];

static void resolve_test(const char *label, const char *in) {
    sceClibMemset(out, 0, sizeof(out));
    const int ret = fios_resolve_range(0, in, out, sizeof(out), 0, 0x7F);
    PRINTF(ret, "%s: %s -> [%s]", label, in, ret < 0 ? "" : fios_short(out));
}

int main(void) {
    fios_init();
    sceIoMkdir(FIOS_DIR, 0777);
    sceIoMkdir(P, 0777);
    sceIoMkdir(P "/dst", 0777);
    sceIoMkdir(P "/dst/sub", 0777);
    sceIoMkdir(P "/src", 0777);
    sceIoMkdir(P "/other", 0777);
    fios_write_file(P "/dst/file");
    fios_write_file(P "/dst/sub/file");
    fios_write_file(P "/src/file");
    fios_write_file(P "/src/sub_file");

    // Overlay: dst/ -> src/
    SceFiosOverlay overlay = fios_overlay(SCE_FIOS_OVERLAY_TYPE_OPAQUE, 0, P "/dst/", P "/src/");
    SceFiosOverlayID id = -1;
    fios_add(&overlay, &id);

    // 1. Dot and Dot-dot inside dst prefix in input
    resolve_test("dot in input after dst", P "/dst/./file");
    resolve_test("dot-dot in input after dst", P "/dst/sub/../file");

    // 2. Dot-dot in input BEFORE dst prefix (reaching dst via ..)
    resolve_test("dot-dot reaching dst", P "/other/../dst/file");

    // 3. Dot-dot attempting to escape dst (leaving dst via ..)
    resolve_test("dot-dot leaving dst", P "/dst/../other/file");

    // 4. Multiple slashes in input
    resolve_test("double slash in input", P "/dst//file");
    resolve_test("multiple slashes in input", P "///dst///file");

    // 5. Backslash in input
    resolve_test("backslash in input", P "\\dst\\file");

    fios_remove(id);

    // 6. Dot-dot inside overlay.dst and overlay.src definitions
    SceFiosOverlay ov_dot = fios_overlay(SCE_FIOS_OVERLAY_TYPE_OPAQUE, 0, P "/other/../dst/", P "/src/");
    SceFiosOverlayID id_dot = -1;
    int add_ret = fios_add(&ov_dot, &id_dot);
    PRINT("add overlay with dot-dot in dst", add_ret);
    if (add_ret >= 0) {
        resolve_test("match overlay defined with dot-dot", P "/dst/file");
        fios_remove(id_dot);
    }

    test_exit();
    return 0;
}

