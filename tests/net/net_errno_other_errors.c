// The libnet errno word, TLS key 0x40: which libnet calls write it when they fail, and what they
// write. Before sceNetInit the test closes a socket and destroys a resolver, after it it closes a
// socket that doesn't exist, makes a socket with an unknown address family and destroys a resolver
// that doesn't exist. Before each call the word is set to a marker, so the output shows whether the
// call wrote it.
//
// On a PS Vita TV on firmware 3.65 both calls before init fail with 0x804101C8. The socket close
// leaves the word at the marker, but the resolver destroy writes 0xC8, the low byte of its result.
// After init the close of socket -1 fails with 0x80410109 and writes 9, the socket with family
// 0x1234 fails with 0x8041012B and writes 0x2B, and the destroy of an unknown resolver fails with
// 0x80410109 and writes 9.
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>

#include "test.h"

#define MARKER 77
#define BAD_FAMILY 0x1234
#define BAD_RESOLVER 0x7FFF

static volatile int *errno_word;

static void reset(void) {
    *errno_word = MARKER;
}

// Prints a call's result, 0 for a created socket, and the errno word after it
static void print_call(const char *name, int result) {
    const int word = *errno_word;
    PRINTF(result < 0 ? result : 0, "%s", name);
    PRINTF(word, "%s: errno", name);
}

int main(void) {
    PRINT("load the net module", sceSysmoduleLoadModule(SCE_SYSMODULE_NET));
    errno_word = sceKernelGetTLSAddr(0x40);
    PRINT("sceNetErrnoLoc is TLS key 0x40", sceNetErrnoLoc() == errno_word);

    reset();
    print_call("close a socket before init", sceNetSocketClose(0));
    reset();
    print_call("destroy a resolver before init", sceNetResolverDestroy(BAD_RESOLVER));

    static char memory[0x20000];
    SceNetInitParam param = { .memory = memory, .size = sizeof(memory), .flags = 0 };
    PRINT("init", sceNetInit(&param));

    reset();
    print_call("close a socket that doesn't exist", sceNetSocketClose(-1));
    reset();
    const int socket = sceNetSocket("test", BAD_FAMILY, SCE_NET_SOCK_DGRAM, 0);
    print_call("socket with an unknown family", socket);
    if (socket >= 0)
        sceNetSocketClose(socket);
    reset();
    print_call("destroy a resolver that doesn't exist", sceNetResolverDestroy(BAD_RESOLVER));

    PRINT("term", sceNetTerm());
    test_exit();
    return 0;
}
