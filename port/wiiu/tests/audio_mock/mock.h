#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint64_t OSTime;
typedef struct { int unused; } OSThread;
typedef unsigned OSThreadAttributes;
typedef struct { int bus_volume; } AXBus;
typedef struct { struct { int volume; } bus[1]; } AXVoiceDeviceMixData;
typedef int AXDeviceType;
typedef struct { int ear; } AXVoice;
typedef struct { unsigned currentOffset,dataType,loopingEnabled,loopOffset,endOffset; void *data; } AXVoiceOffsets;
typedef struct { int renderer,pipeline; } AXInitParams;
typedef struct { unsigned volume,delta; } AXVoiceVeData;
typedef struct { unsigned ratio; } AXVoiceSrc;
#define AX_DEVICE_TYPE_TV 0
#define AX_DEVICE_TYPE_DRC 1
#define AX_INIT_RENDERER_48KHZ 0
#define AX_INIT_PIPELINE_SINGLE 0
#define AX_VOICE_FORMAT_LPCM16 0
#define AX_VOICE_LOOP_ENABLED 1
#define AX_VOICE_SRC_TYPE_NONE 0
#define AX_VOICE_STATE_PLAYING 1
#define AX_VOICE_STATE_STOPPED 0
#define OS_THREAD_ATTRIB_AFFINITY_CPU0 1
#define OS_THREAD_ATTRIB_DETACHED 2
void *MEMAllocFromDefaultHeapEx(size_t,int); void MEMFreeToDefaultHeap(void *);
void DCFlushRange(void *,size_t);
int AXIsInit(void); void AXInitWithParams(AXInitParams *); void AXQuit(void);
AXVoice *AXAcquireVoice(int,void *,void *); void AXFreeVoice(AXVoice *);
void AXVoiceBegin(AXVoice *); void AXVoiceEnd(AXVoice *);
void AXSetVoiceDeviceMix(AXVoice *,int,int,AXVoiceDeviceMixData *);
void AXSetVoiceType(AXVoice *,int); void AXSetVoiceOffsets(AXVoice *,AXVoiceOffsets *);
void AXSetVoiceVe(AXVoice *,AXVoiceVeData *); void AXSetVoiceSrc(AXVoice *,AXVoiceSrc *);
void AXSetVoiceSrcType(AXVoice *,int); void AXSetVoiceState(AXVoice *,int);
void AXGetVoiceOffsets(AXVoice *,AXVoiceOffsets *);
OSTime OSGetSystemTime(void); OSTime OSMillisecondsToTicks(unsigned);
void OSSleepTicks(OSTime); int OSCreateThread(OSThread *,int (*)(int,const char **),int,void *,void *,int,int,OSThreadAttributes);
void OSSetThreadName(OSThread *,const char *); void OSResumeThread(OSThread *);
