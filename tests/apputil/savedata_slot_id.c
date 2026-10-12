// Save data slot IDs: which ones the slot and data calls take, around the limit of 256, and
// searches that cross it
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>

#include "test.h"

// Missing from vita-headers
typedef struct {
    void *buf;
    SceSize bufSize;
    SceUInt8 reserved[32];
} SceAppUtilWorkBuffer;

typedef struct {
    SceUInt32 type;
    SceUInt32 from;
    SceUInt32 range;
    SceUInt32 key;
    SceUInt32 order;
    SceUInt8 reserved[32];
} SceAppUtilSaveDataSlotSearchCond;

typedef struct {
    SceUInt32 hitNum;
    SceAppUtilSaveDataSlot *slotList;
    SceUInt8 reserved[32];
} SceAppUtilSlotSearchResult;

int sceAppUtilSaveDataSlotSearch(SceAppUtilWorkBuffer *workBuf, const SceAppUtilSaveDataSlotSearchCond *cond,
    SceAppUtilSlotSearchResult *result, const SceAppUtilSaveDataMountPoint *mountPoint);

#define SEARCH_EMPTY 1

static SceAppUtilSaveDataSlot slots[512];

static void try_slot(unsigned int id) {
    SceAppUtilSaveDataSlotParam param;
    sceClibMemset(&param, 0, sizeof(param));
    PRINTF(sceAppUtilSaveDataSlotCreate(id, &param, NULL), "create slot 0x%X", id);
    PRINTF(sceAppUtilSaveDataSlotGetParam(id, &param, NULL), "get the parameters of slot 0x%X", id);

    static char data[] = "slot";
    char path[32];
    sceClibSnprintf(path, sizeof(path), "slot_%X.bin", id);
    SceAppUtilSaveDataFile file;
    sceClibMemset(&file, 0, sizeof(file));
    file.filePath = path;
    file.buf = data;
    file.bufSize = sizeof(data);
    SceAppUtilSaveDataFileSlot slot;
    sceClibMemset(&slot, 0, sizeof(slot));
    slot.id = id;
    SceSize required = 0xCCCCCCCC;
    PRINTF(sceAppUtilSaveDataDataSave(&slot, &file, 1, NULL, &required), "save data to slot 0x%X", id);
    PRINTF(required, "the size it needs for slot 0x%X", id);

    SceAppUtilSaveDataRemoveItem item;
    sceClibMemset(&item, 0, sizeof(item));
    item.dataPath = path;
    PRINTF(sceAppUtilSaveDataDataRemove(&slot, &item, 1, NULL), "remove the data of slot 0x%X", id);
    PRINTF(sceAppUtilSaveDataSlotDelete(id, NULL), "delete slot 0x%X", id);
}

static void try_search(unsigned int from, unsigned int range) {
    SceAppUtilWorkBuffer work = { .buf = slots, .bufSize = sizeof(slots) };
    SceAppUtilSaveDataSlotSearchCond cond;
    sceClibMemset(&cond, 0, sizeof(cond));
    cond.type = SEARCH_EMPTY;
    cond.from = from;
    cond.range = range;
    SceAppUtilSlotSearchResult result;
    sceClibMemset(&result, 0, sizeof(result));
    PRINTF(sceAppUtilSaveDataSlotSearch(&work, &cond, &result, NULL), "search empty slots from %u for %u", from, range);
    PRINTF(result.hitNum, "slots found from %u for %u", from, range);
    if (result.hitNum) {
        PRINTF(slots[0].id, "first slot found from %u for %u", from, range);
        PRINTF(slots[result.hitNum - 1].id, "last slot found from %u for %u", from, range);
    }
}

int main(void) {
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    sceClibMemset(&init, 0, sizeof(init));
    sceClibMemset(&boot, 0, sizeof(boot));
    PRINT("init", sceAppUtilInit(&init, &boot));

    try_slot(0);
    try_slot(255);
    try_slot(256);
    try_slot(0x1000);
    try_slot(0xFFFFFFFF);
    try_search(250, 6);
    try_search(250, 10);
    try_search(0, 300);

    sceAppUtilShutdown();
    test_exit();
    return 0;
}
