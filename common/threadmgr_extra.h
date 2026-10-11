// Thread manager calls and constants that the vitasdk headers lack
#pragma once

#include <psp2/kernel/threadmgr.h>
#include <psp2/types.h>

// Message pipe memory type, 0x40 is what games pass
#define MSG_PIPE_TYPE_USER 0x40

#define MSG_PIPE_MODE_ASAP 0x0
#define MSG_PIPE_MODE_FULL 0x1

// Queues the waiters of an object by priority instead of in arrival order
#define ATTR_TH_PRIO 0x2000

#define MSG_PIPE_MODE_DONT_REMOVE 0x100

// Lets the owner lock a mutex more than once, for heavy and lightweight mutexes
#define MUTEX_ATTR_RECURSIVE 0x2

// Event flag wait modes, AND or OR, plus optionally one way of clearing the bits after the wait
#define EVF_WAITMODE_AND 0x0
#define EVF_WAITMODE_OR 0x1
#define EVF_WAITMODE_CLEAR_ALL 0x2
#define EVF_WAITMODE_CLEAR_PAT 0x4

// Lets more than one thread wait on an event flag at a time
#define EVF_ATTR_MULTI 0x1000

// The event bit a timer sets when its event fires
#define TIMER_EVENT_BIT 0x8000

SceUID sceKernelCreateSimpleEvent(const char *name, SceUInt32 attr, SceUInt32 init_pattern, const void *opt);
SceInt32 sceKernelDeleteSimpleEvent(SceUID id);
SceInt32 sceKernelSetEvent(SceUID id, SceUInt32 pattern, SceUInt64 user_data);
SceInt32 sceKernelPulseEvent(SceUID id, SceUInt32 pattern, SceUInt64 user_data);
SceInt32 sceKernelGetEventPattern(SceUID id, SceUInt32 *pattern);
SceInt32 sceKernelClearEvent(SceUID id, SceUInt32 clear_pattern);
SceInt32 sceKernelPollEvent(SceUID id, SceUInt32 pattern, SceUInt32 *result, SceUInt64 *user_data);
SceInt32 sceKernelWaitEvent(SceUID id, SceUInt32 pattern, SceUInt32 *result, SceUInt64 *user_data, SceUInt32 *timeout);

// Returns the class of uid, or an error. The vitasdk declaration returns an enum of the PSP
// classes, which the compiler assumes is never negative, so this declares the call again as
// returning SceInt32.
SceInt32 get_uid_class(SceUID uid) __asm__("sceKernelGetThreadmgrUIDClass");

SceInt32 sceKernelGetCondInfo(SceUID id, SceKernelCondInfo *info);

// What sceKernelGetMutexInfo fills. The vitasdk SceKernelMutexInfo lacks ceilingPriority.
typedef struct MutexInfo {
    SceSize size;
    SceUID mutexId;
    char name[32];
    SceUInt32 attr;
    SceInt32 initCount;
    SceInt32 currentCount;
    SceUID currentOwnerId;
    SceUInt32 numWaitThreads;
    SceInt32 ceilingPriority;
} MutexInfo;

// What sceKernelGetCallbackInfo fills. The vitasdk SceKernelCallbackInfo lacks attr and notifyId,
// so its size is wrong.
typedef struct CallbackInfo {
    SceSize size;
    SceUID callbackId;
    char name[32];
    SceUInt32 attr;
    SceUID threadId;
    SceKernelCallbackFunction callback;
    SceUID notifyId;
    SceInt32 notifyCount;
    SceInt32 notifyArg;
    void *common;
} CallbackInfo;

SceInt32 sceKernelCancelEventFlag(SceUID id, SceUInt32 set_pattern, SceUInt32 *num_wait_threads);

// Simple event bits a game may set, in its 0xFF000000 range. The other bits are system events, like
// the create event 0x4 that every new event starts with, and setting them fails.
#define EVENT_BIT_A 0x01000000
#define EVENT_BIT_B 0x02000000
#define EVENT_BIT_C 0x04000000
#define EVENT_BIT_D 0x08000000

// The system event a simple event reports to its waiters when it is deleted
#define EVENT_DELETE 0x8

// Event object attributes, for simple events and timers
#define EVENT_ATTR_MANUAL_RESET 0x0
#define EVENT_ATTR_AUTO_RESET 0x100

// Lets sceKernelOpen* find an object by its name
#define ATTR_OPENABLE 0x80

// The user cores a thread may run on, for its CPU affinity mask
#define CPU_MASK_USER_0 0x10000
#define CPU_MASK_USER_1 0x20000
#define CPU_MASK_USER_2 0x40000

#define EVENT_WAIT_MODE_OR 0x1
#define EVENT_WAIT_MODE_AND 0x2

typedef struct WaitEvent {
    SceUID eventId;
    SceUInt32 eventPattern;
} WaitEvent;

typedef struct ResultEvent {
    SceUID eventId;
    SceInt32 result;
    SceUInt32 resultPattern;
    SceInt32 reserved;
    SceUInt64 userData;
} ResultEvent;

typedef struct MsgPipeVector {
    void *base;
    SceSize size;
} MsgPipeVector;

SceInt32 sceKernelWaitMultipleEvents(WaitEvent *events, SceUInt32 count, SceUInt32 mode, ResultEvent *results, SceUInt32 *timeout);
SceInt32 sceKernelWaitMultipleEventsCB(WaitEvent *events, SceUInt32 count, SceUInt32 mode, ResultEvent *results, SceUInt32 *timeout);
SceInt32 sceKernelRegisterCallbackToEvent(SceUID event_id, SceUID callback_id);
SceInt32 sceKernelSendMsgPipeVector(SceUID id, const MsgPipeVector *vectors, SceUInt32 count, SceUInt32 mode, SceSize *result, SceUInt32 *timeout);
SceInt32 sceKernelReceiveMsgPipeVector(SceUID id, const MsgPipeVector *vectors, SceUInt32 count, SceUInt32 mode, SceSize *result, SceUInt32 *timeout);

SceUID sceKernelCreateTimer(const char *name, SceUInt32 attr, const void *opt);
SceInt32 sceKernelCancelTimer(SceUID id, SceUInt32 *num_wait_threads);
SceUInt64 sceKernelGetTimerBaseWide(SceUID id);
SceInt32 sceKernelStartTimer(SceUID id);
SceInt32 sceKernelStopTimer(SceUID id);
SceUInt64 sceKernelGetTimerTimeWide(SceUID id);
SceUInt64 sceKernelSetTimerTimeWide(SceUID id, SceUInt64 time);
SceInt32 sceKernelSetTimerEvent(SceUID id, SceInt32 type, SceUInt64 *interval, SceInt32 repeat);

SceInt32 sceKernelLockLwMutexCB(SceKernelLwMutexWork *work, SceInt32 count, SceUInt32 *timeout);

// What sceKernelGetLwMutexInfo and sceKernelGetLwMutexInfoById fill
typedef struct LwMutexInfo {
    SceSize size;
    SceUID uid;
    char name[32];
    SceUInt32 attr;
    SceKernelLwMutexWork *work;
    SceInt32 initCount;
    SceInt32 currentCount;
    SceUID currentOwnerId;
    SceUInt32 numWaitThreads;
} LwMutexInfo;

SceInt32 sceKernelGetLwMutexInfo(SceKernelLwMutexWork *work, LwMutexInfo *info);
SceInt32 sceKernelGetLwMutexInfoById(SceUID id, LwMutexInfo *info);

// What sceKernelGetLwCondInfo and sceKernelGetLwCondInfoById fill
typedef struct LwCondInfo {
    SceSize size;
    SceUID uid;
    char name[32];
    SceUInt32 attr;
    SceKernelLwCondWork *work;
    SceKernelLwMutexWork *lwMutex;
    SceUInt32 numWaitThreads;
} LwCondInfo;

SceInt32 sceKernelGetLwCondInfo(SceKernelLwCondWork *work, LwCondInfo *info);
SceInt32 sceKernelGetLwCondInfoById(SceUID id, LwCondInfo *info);

SceInt32 sceKernelCancelRWLock(SceUID id, SceUInt32 *num_read_waiters, SceUInt32 *num_write_waiters, SceInt32 flag);

SceUID sceKernelOpenEventFlag(const char *name);
SceInt32 sceKernelCloseEventFlag(SceUID id);
SceUID sceKernelOpenMsgPipe(const char *name);
SceInt32 sceKernelCloseMsgPipe(SceUID id);
SceUID sceKernelOpenSimpleEvent(const char *name);
SceInt32 sceKernelCloseSimpleEvent(SceUID id);
SceUID sceKernelOpenTimer(const char *name);
SceInt32 sceKernelCloseTimer(SceUID id);
SceInt32 sceKernelDeleteTimer(SceUID id);

// What sceKernelGetEventInfo fills, for simple events and timers
typedef struct EventInfo {
    SceSize size;
    SceUID eventId;
    char name[32];
    SceUInt32 attr;
    SceUInt32 eventPattern;
    SceUInt64 userData;
    SceUInt32 numWaitThreads;
    SceInt32 reserved;
} EventInfo;

SceInt32 sceKernelGetEventInfo(SceUID id, EventInfo *info);
SceInt32 sceKernelWaitEventCB(SceUID id, SceUInt32 pattern, SceUInt32 *result, SceUInt64 *user_data, SceUInt32 *timeout);
SceInt32 sceKernelCancelEvent(SceUID id, SceUInt32 *num_wait_threads);

// What sceKernelGetTimerInfo fills
typedef struct TimerInfo {
    SceSize size;
    SceUID timerId;
    char name[32];
    SceUInt32 attr;
    SceInt32 active;
    SceKernelSysClock baseTime;
    SceKernelSysClock currentTime;
    SceKernelSysClock schedule;
    SceKernelSysClock interval;
    SceInt32 type;
    SceInt32 repeat;
    SceUInt32 numWaitThreads;
    SceInt32 reserved;
} TimerInfo;

SceInt32 sceKernelGetTimerInfo(SceUID id, TimerInfo *info);
SceInt32 sceKernelGetTimerTime(SceUID id, SceKernelSysClock *clock);
SceInt32 sceKernelSetTimerTime(SceUID id, SceKernelSysClock *clock);
SceInt32 sceKernelGetTimerBase(SceUID id, SceKernelSysClock *base);
