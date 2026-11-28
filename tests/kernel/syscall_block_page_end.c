// The wait and pread syscalls with an argument block in the last 8 bytes of a memory block,
// when the page after it isn't mapped. The fields these calls use fit in the 8 bytes.
//
// On a PS Vita TV on firmware 3.65 each call gives 0x80022005, as it copies 0x10 bytes of the block.
// The same block 0x10 bytes from the end works.
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>

#include "syscalls.h"
#include "test.h"

#define IO_DIR "ux0:data/vita-autotests/io"
#define FILE_A IO_DIR "/page_end_a.bin"

// Tells if any memory block or other mapping holds addr, by both lookups
static int is_mapped(void *addr) {
    SceKernelMemBlockInfo info;
    info.size = sizeof(info);
    return sceKernelFindMemBlockByAddr(addr, 0) >= 0 || sceKernelGetMemBlockInfoByAddr(addr, &info) >= 0;
}

int main(void) {
    // Pages are placed where the allocator likes, so take the first one with no page mapped after it
    SceUID blocks[16];
    SceUID block = -1;
    char *page = NULL;
    int count = 0;
    while (count < 16 && block < 0) {
        blocks[count] = sceKernelAllocMemBlock("page", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL);
        if (blocks[count] < 0) {
            PRINT("allocate a page", blocks[count]);
            test_exit();
            return 0;
        }
        void *base = NULL;
        sceKernelGetMemBlockBase(blocks[count], &base);
        if (!is_mapped((char *)base + 0x1000)) {
            block = blocks[count];
            page = base;
        }
        count++;
    }
    for (int i = 0; i < count; i++)
        if (blocks[i] != block)
            sceKernelFreeMemBlock(blocks[i]);
    if (block < 0) {
        test_skip("no page with nothing mapped after it");
        return 0;
    }
    char *const end = page + 0x1000 - 8;
    char *const inside = page + 0x1000 - 0x10;

    const SceUID flag = sceKernelCreateEventFlag("flag", 0, 0x1, NULL);
    sceClibMemset(page, 0, 0x1000);
    PRINT("wait on the flag with the block at the end", syscall_wait_event_flag(flag, 0x1, 0, (const WaitEventFlagBlock *)end));
    PRINT("wait on the flag with the block 0x10 from the end", syscall_wait_event_flag(flag, 0x1, 0, (const WaitEventFlagBlock *)inside));
    sceKernelDeleteEventFlag(flag);

    const SceUID event = sceKernelCreateSimpleEvent("event", 0, 0, NULL);
    sceKernelSetEvent(event, 0x01000000, 0);
    SceUInt32 result = 0;
    PRINT("wait on the event with the block at the end", syscall_wait_event(event, 0x01000000, &result, (const WaitEventBlock *)end));
    PRINT("wait on the event with the block 0x10 from the end", syscall_wait_event(event, 0x01000000, &result, (const WaitEventBlock *)inside));
    sceKernelDeleteSimpleEvent(event);

    sceIoMkdir(IO_DIR, 0777);
    const SceUID fd = sceIoOpen(FILE_A, SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    sceIoWrite(fd, "0123456789", 10);
    char buf[4];
    PRINT("pread with the offset block at the end", syscall_io_pread(fd, buf, 4, (const IoOffsetBlock *)end));
    PRINT("pread with the offset block 0x10 from the end", syscall_io_pread(fd, buf, 4, (const IoOffsetBlock *)inside));
    sceIoClose(fd);
    sceIoRemove(FILE_A);

    sceKernelFreeMemBlock(block);
    test_exit();
    return 0;
}
