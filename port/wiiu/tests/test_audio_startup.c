#include "mock.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
static int init, fail_init, alloc_attempt, voice_attempt, fail_alloc, fail_voice, fail_thread;
static int allocated, active_voices, quit_calls;
static AXVoice mock_voices[2];
void *MEMAllocFromDefaultHeapEx(size_t n,int a) { (void)a; if (++alloc_attempt==fail_alloc) return NULL; void *p=malloc(n); if(p) allocated++; return p; }
void MEMFreeToDefaultHeap(void *p) { if(p) { allocated--; free(p); } }
void DCFlushRange(void *p,size_t n) {(void)p;(void)n;}
int AXIsInit(void) {return init;}
void AXInitWithParams(AXInitParams *p) {(void)p;if(!fail_init)init=1;}
void AXQuit(void) {assert(init);init=0;quit_calls++;}
AXVoice *AXAcquireVoice(int p,void *a,void *b) {(void)p;(void)a;(void)b; if(++voice_attempt==fail_voice)return NULL; active_voices++; return &mock_voices[voice_attempt-1];}
void AXFreeVoice(AXVoice *v) {(void)v;active_voices--;}
void AXVoiceBegin(AXVoice *v){(void)v;} void AXVoiceEnd(AXVoice *v){(void)v;}
void AXSetVoiceDeviceMix(AXVoice *v,int d,int x,AXVoiceDeviceMixData *m){(void)v;(void)d;(void)x;(void)m;}
void AXSetVoiceType(AXVoice *v,int x){(void)v;(void)x;} void AXSetVoiceOffsets(AXVoice *v,AXVoiceOffsets *o){(void)v;(void)o;}
void AXSetVoiceVe(AXVoice *v,AXVoiceVeData *d){(void)v;(void)d;} void AXSetVoiceSrc(AXVoice *v,AXVoiceSrc *d){(void)v;(void)d;}
void AXSetVoiceSrcType(AXVoice *v,int t){(void)v;(void)t;} void AXSetVoiceState(AXVoice *v,int s){(void)v;(void)s;}
void AXGetVoiceOffsets(AXVoice *v,AXVoiceOffsets *o){(void)v;o->currentOffset=0;}
OSTime OSGetSystemTime(void){return 0;} OSTime OSMillisecondsToTicks(unsigned v){return v;}
void OSSleepTicks(OSTime t){(void)t;} int OSCreateThread(OSThread *t,int (*f)(int,const char **),int a,void *b,void *c,int d,int e,OSThreadAttributes g){(void)t;(void)f;(void)a;(void)b;(void)c;(void)d;(void)e;(void)g;return !fail_thread;}
void OSSetThreadName(OSThread *t,const char *n){(void)t;(void)n;} void OSResumeThread(OSThread *t){(void)t;}
#include "../src/wiiu_audio.c"
void wiiu_log(const char *fmt,...){(void)fmt;}
static void mixer(float *p,unsigned long n){(void)p;(void)n;}
static void reset_mocks(void){alloc_attempt=voice_attempt=fail_alloc=fail_voice=fail_thread=fail_init=0;quit_calls=0;}
static void clean(void){voices_stop(1);started=0;suspended=0;assert(!allocated && !active_voices);}
int main(void){
 int i;
 for(i=1;i<=2;i++){ reset_mocks();fail_alloc=i;assert(!wiiu_audio_open(mixer));assert(!allocated&&!active_voices&&!init&&quit_calls==1); }
 for(i=1;i<=2;i++){ reset_mocks();fail_voice=i;assert(!wiiu_audio_open(mixer));assert(!allocated&&!active_voices&&!init&&quit_calls==1); }
 reset_mocks();fail_init=1;assert(!wiiu_audio_open(mixer));assert(!allocated&&!active_voices&&!init&&!quit_calls);
 reset_mocks();fail_thread=1;assert(!wiiu_audio_open(mixer));assert(!allocated&&!active_voices&&!init&&quit_calls==1);
 reset_mocks();init=1;fail_voice=2;assert(!wiiu_audio_open(mixer));assert(!allocated&&!active_voices&&init&&!quit_calls);init=0;
 reset_mocks();assert(wiiu_audio_open(mixer));assert(allocated==2&&active_voices==2);wiiu_audio_suspend();assert(suspended&&allocated==2&&!active_voices&&!init);
 reset_mocks();fail_voice=2;wiiu_audio_resume();assert(suspended&&!allocated&&!active_voices&&!init);
 reset_mocks();wiiu_audio_resume();assert(!suspended&&allocated==2&&active_voices==2&&init);clean();
 puts("audio startup, failure cleanup, and retry: all passed");return 0;
}
