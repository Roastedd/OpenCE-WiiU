"""Pool-backed packets through the actual queue, worker and completion C.

Only platform calls and the worker's test barrier are substituted. The worker
is stopped at its empty-queue condition wait, never during ownership transfer.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from harness import function, structure
from test_wiiu_adpcm import table

PREFIX = r'''
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
static unsigned fail_next;
static unsigned long heap_calls, heap_frees;
static void *pool_malloc(size_t bytes) { __atomic_add_fetch(&heap_calls,1,__ATOMIC_RELAXED); if(fail_next) { fail_next--; return NULL; } return malloc(bytes); }
static void pool_free(void *memory) { if(memory)__atomic_add_fetch(&heap_frees,1,__ATOMIC_RELAXED);free(memory); }
/* Preserve the real separate-module public call boundary in timings. */
__attribute__((noinline)) void *wiiu_packet_pool_allocate(size_t bytes);
__attribute__((noinline)) void wiiu_packet_pool_release(void *buffer);
#define malloc pool_malloc
#define free pool_free
#include POOL_SOURCE
#undef malloc
#undef free
#include "wiiu_decoder_profile.h"
#define HALO_WIIU
#define DSOUND_DECODE_THREAD
#define DSOUND_MIXER_YIELDS
#define STDMETHODCALLTYPE
#define MAXIMUM_STREAM_PACKETS 64
#define MAXIMUM_OUTPUT_CHANNELS 6
#define RESAMPLER_HISTORY 128
#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#ifdef PACKET_MEMORY_BASELINE
#define packet_allocate pool_malloc
#define packet_deallocate pool_free
#else
#define packet_allocate wiiu_packet_pool_allocate
#define packet_deallocate wiiu_packet_pool_release
#endif
typedef int BOOL;
typedef uint32_t DWORD, ULONG;
typedef int32_t LONG, HRESULT;
typedef void *LPVOID;
typedef struct { void *lpVtbl; } IDirectSoundStream;
typedef struct { void *pvBuffer; DWORD dwMaxSize; DWORD *pdwCompletedSize, *pdwStatus; void *hCompletionEvent, *pContext; } XMEDIAPACKET;
typedef const XMEDIAPACKET *LPCXMEDIAPACKET;
typedef void (*LPFNXMEDIAOBJECTCALLBACK)(void *, void *, DWORD);
static unsigned callbacks, event_calls, resets;
static DWORD last_status;
static void SetEvent(void *event) { (void)event; event_calls++; }
static void platform_log(const char *format, ...) { (void)format; }
static uint64_t OSGetSystemTime(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u; }
#define OSMillisecondsToTicks(value) ((uint64_t)(value)*1000u)
#define OSTicksToMicroseconds(value) (value)
void wiiu_thread_beside_gx2(void) { }
'''

BEFORE_FUNCTIONS = r'''
static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_stream *streams, *mixer_walk_next;
static int mixer_lock_wanted;
static unsigned long packet_serial, decoder_voices_waited, decoder_profile_fallbacks;
static void resampler_reset(struct sdl_stream *stream) { stream->cursor=0;stream->center=0;stream->phase=0;resets++; }
static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);
static pthread_mutex_t barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_condition = PTHREAD_COND_INITIALIZER;
static int pause_decode, entered_decode, worker_stopping, retain_stream;
static void *retired_stream;
static void stream_memory_release(void *memory) { if(retain_stream) retired_stream=memory; else free(memory); }
static int worker_wait(pthread_cond_t *condition, pthread_mutex_t *lock) {
 if(worker_stopping) { pthread_mutex_unlock(lock);pthread_exit(NULL); }
 return pthread_cond_wait(condition,lock);
}
'''

AFTER_FUNCTIONS = r'''
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr,"packet lifetime failed at %d: %s\n",__LINE__,#condition);return 1; } } while(0)
static void callback(void *context, void *packet, DWORD status) { (void)context;(void)packet;callbacks++;last_status=status; }
static struct sdl_stream *voice(int adpcm, unsigned channels) {
 struct sdl_stream *s;if(retired_stream){s=retired_stream;retired_stream=NULL;memset(s,0,sizeof(*s));}else s=calloc(1,sizeof(*s));s->reference_count=1;s->adpcm=adpcm;s->channels=channels;s->sample_rate=22050;s->callback=callback;s->next=streams;streams=s;return s;
}
static int empty_queue_cases(void) {
 struct sdl_stream *s=voice(0,2);short source[4]={-32768,32767,17,-53};DWORD status=99,completed=99;
 XMEDIAPACKET input={source,sizeof(source),&completed,&status,NULL,NULL};
 CHECK(stream_process((IDirectSoundStream*)s,&input,NULL)==S_OK);
 CHECK(s->packet_count==1&&s->packets[0].ready&&s->packets[0].frames==2);
 CHECK(!memcmp(s->packets[0].samples,source,sizeof(source))&&status==XMEDIAPACKET_STATUS_PENDING&&completed==0);
 s->packets[0].finished=TRUE;streams_complete_finished();
 CHECK(s->packet_count==0&&callbacks==1&&last_status==XMEDIAPACKET_STATUS_SUCCESS&&completed==sizeof(source));
 /* A full decoder queue retains the old synchronous decode fallback. */
 decoder.started=TRUE;decoder.count=DECODER_JOBS;
 CHECK(stream_process((IDirectSoundStream*)s,&input,NULL)==S_OK);
 CHECK(s->packets[s->packet_head].ready&&!memcmp(s->packets[s->packet_head].samples,source,sizeof(source)));
 decoder.count=0;stream_flush((IDirectSoundStream*)s);
 CHECK(last_status==XMEDIAPACKET_STATUS_FLUSHED&&s->packet_count==0);
 /* Failure to copy still decodes synchronously from the live input. */
 wiiu_packet_pool_trim();fail_next=1;
 CHECK(stream_process((IDirectSoundStream*)s,&input,NULL)==S_OK);
 CHECK(s->packets[s->packet_head].ready&&!memcmp(s->packets[s->packet_head].samples,source,sizeof(source)));
 stream_flush((IDirectSoundStream*)s);
 CHECK(stream_release((IDirectSoundStream*)s)==0&&!streams);
 decoder.started=FALSE;
 wiiu_packet_pool_trim();
 puts("PCM completion, full-queue fallback and source-copy OOM fallback passed");return 0;
}
static int wait_decode(void) {
 unsigned tries=0;struct timespec delay={0,1000000};
 for(;;) { int entered;pthread_mutex_lock(&barrier_lock);entered=entered_decode;pthread_mutex_unlock(&barrier_lock);if(entered)return 1;if(++tries==3000)return 0;nanosleep(&delay,NULL); }
}
static int wait_ready(struct sdl_stream *stream, unsigned long serial) {
 unsigned tries=0;struct timespec delay={0,1000000};
 for(;;) { int ready;pthread_mutex_lock(&mixer_lock);ready=stream->packet_count&&stream->packets[stream->packet_head].serial==serial&&stream->packets[stream->packet_head].ready;pthread_mutex_unlock(&mixer_lock);if(ready)return 1;if(++tries==3000)return 0;nanosleep(&delay,NULL); }
}
static void resume_decode(void) { pthread_mutex_lock(&barrier_lock);pause_decode=0;pthread_cond_broadcast(&barrier_condition);pthread_mutex_unlock(&barrier_lock); }
static int queued_ownership(int release_voice) {
 struct sdl_stream *s=voice(1,1);pthread_t worker;unsigned char *original=malloc(36);unsigned i;unsigned long serial;
 XMEDIAPACKET input={original,36,NULL,NULL,NULL,NULL};
 memset(original,0,36);original[0]=7;
 pthread_mutex_lock(&barrier_lock);pause_decode=1;entered_decode=0;pthread_mutex_unlock(&barrier_lock);
 decoder.started=TRUE;CHECK(!pthread_create(&worker,NULL,decoder_thread,NULL));
 CHECK(stream_process((IDirectSoundStream*)s,&input,NULL)==S_OK);serial=packet_serial;
 CHECK(wait_decode());
 memset(original,255,36);free(original);
 if(release_voice) { struct sdl_stream *old=s;retain_stream=release_voice==2;CHECK(stream_release((IDirectSoundStream*)s)==0);s=voice(1,1);if(retain_stream)CHECK(s==old);retain_stream=0; }
 else stream_flush((IDirectSoundStream*)s);
 /* Returned buffers may be scribbled/reused, but the worker's source is
 still owned by that job, not by the flushed packet or released voice. */
 { void *other[32];for(i=0;i<32;i++){other[i]=wiiu_packet_pool_allocate(36);CHECK(other[i]);memset(other[i],0xa5,36);}for(i=0;i<32;i++)wiiu_packet_pool_release(other[i]); }
 /* The stale publication may find the new stream at the same address;
 its new serial must still prevent accepting the old decoded packet. */
 { unsigned char next[72]={0};XMEDIAPACKET packet={next,72,NULL,NULL,NULL,NULL};next[0]=11;
  CHECK(stream_process((IDirectSoundStream*)s,&packet,NULL)==S_OK);serial=packet_serial;resume_decode();CHECK(wait_ready(s,serial));
  CHECK(s->packet_count==1&&s->packets[s->packet_head].frames==128);
  for(i=0;i<64;i++)CHECK(s->packets[s->packet_head].samples[i]==11);
  CHECK(s->packets[s->packet_head].samples[64]==0);
 }
 pthread_mutex_lock(&decoder.lock);worker_stopping=1;pthread_cond_broadcast(&decoder.posted);pthread_mutex_unlock(&decoder.lock);
 CHECK(!pthread_join(worker,NULL));worker_stopping=0;decoder.started=FALSE;
 stream_flush((IDirectSoundStream*)s);CHECK(stream_release((IDirectSoundStream*)s)==0&&!streams);
 wiiu_packet_pool_trim();puts(release_voice?"release during decode and stale publication passed":"flush during decode and stale publication passed");return 0;
}
static uint32_t checksum=2166136261u;
static int pipeline_bytes;
static int pipeline(int pcm) {
 struct sdl_stream *voices[64];unsigned char source[32768];unsigned wave,i,n,size=pcm?32768:2304;uint64_t started=0,elapsed;
 struct wiiu_packet_pool_stats memory;
 memset(source,0,sizeof(source));for(i=0;i<size;i+=36)source[i]=(unsigned char)(i/36);
 for(i=0;i<64;i++)voices[i]=voice(!pcm,pcm?2:1);
 decoder.started=TRUE;
 for(wave=0;wave<=(pipeline_bytes?1u:100u);wave++) {
  XMEDIAPACKET input={source,size,NULL,NULL,NULL,NULL};
  if(wave==1) { heap_calls=heap_frees=0;checksum=2166136261u;wiiu_packet_pool_stats(&memory,1);started=OSGetSystemTime(); }
  for(i=0;i<64;i++)CHECK(stream_process((IDirectSoundStream*)voices[i],&input,NULL)==S_OK);
  while(decoder.count) {
   struct decoder_job job;unsigned long frames=0;short *samples;
   pthread_mutex_lock(&decoder.lock);job=decoder.jobs[decoder.first];decoder.first=(decoder.first+1)%DECODER_JOBS;decoder.count--;pthread_mutex_unlock(&decoder.lock);
   samples=packet_decode(job.adpcm,job.source,job.size,job.channels,&frames);
   CHECK(samples);packet_deallocate(job.source);packet_samples_give(job.stream,job.serial,samples,frames);
  }
  for(i=0;i<64;i++) {
   struct voice_packet *packet=&voices[i]->packets[voices[i]->packet_head];
   CHECK(packet->ready&&packet->samples);
   if(pipeline_bytes&&wave==1)CHECK(fwrite(packet->samples,sizeof(short)*voices[i]->channels,packet->frames,stdout)==packet->frames);
   for(n=0;n<packet->frames*voices[i]->channels*sizeof(short);n++)checksum=(checksum^((unsigned char*)packet->samples)[n])*16777619u;
   packet->finished=TRUE;
  }
  streams_complete_finished();
 }
 elapsed=OSGetSystemTime()-started;wiiu_packet_pool_stats(&memory,0);
 if(!pipeline_bytes)printf("{\"format\":\"%s\",\"waves\":100,\"voices\":64,\"packets\":6400,\"elapsed_us\":%llu,\"malloc_calls\":%lu,\"free_calls\":%lu,\"pool_hits\":%u,\"cached_bytes\":%u,\"checksum\":\"%08x\"}\n",pcm?"stereo-pcm":"mono-adpcm",(unsigned long long)elapsed,heap_calls,heap_frees,memory.hits,memory.cached_bytes,checksum);
 decoder.started=FALSE;for(i=0;i<64;i++)stream_release((IDirectSoundStream*)voices[i]);wiiu_packet_pool_trim();return 0;
}
int main(int argc,char **argv) {
 if(argc>1&&!strcmp(argv[1],"bytes-adpcm")){pipeline_bytes=1;return pipeline(0);}
 if(argc>1&&!strcmp(argv[1],"bytes-pcm")){pipeline_bytes=1;return pipeline(1);}
 if(argc>1&&!strcmp(argv[1],"pipeline-adpcm"))return pipeline(0);
 if(argc>1&&!strcmp(argv[1],"pipeline-pcm"))return pipeline(1);
 if(argc>1&&!strcmp(argv[1],"release"))return queued_ownership(1);
 if(argc>1&&!strcmp(argv[1],"aba"))return queued_ownership(2);
 if(argc>1&&!strcmp(argv[1],"flush"))return queued_ownership(0);
 return empty_queue_cases();
}
'''


def generated():
    text = (ROOT / 'port/linux/src/dsound_sdl.c').read_text()
    status_text = (ROOT / 'port/include/xdk/xdk_dsound.h').read_text()
    statuses = '\n'.join(line for line in status_text.splitlines()
                         if any(line.startswith('#define ' + name + ' ') for name in
                                ('XMEDIAPACKET_STATUS_SUCCESS', 'XMEDIAPACKET_STATUS_FLUSHED', 'XMEDIAPACKET_STATUS_PENDING')))
    declarations = text[text.index('#define DECODER_JOBS 256'):text.index('static short *packet_decode')]
    result = PREFIX + statuses + '\n' + structure(text, 'voice_packet') + '\n' + structure(text, 'sdl_stream')
    result += '\n' + BEFORE_FUNCTIONS + declarations + '\n' + table(text) + '\n'
    names = ['mixer_lock_take', 'ima_expand', 'decode_adpcm', 'decode_pcm', 'stream_from_interface',
             'packet_release', 'stream_complete_head', 'streams_complete_finished',
             'packet_samples_give', 'packet_decode']
    result += '\n'.join(function(text, name) for name in names)
    result += '\n#define free stream_memory_release\n' + function(text, 'stream_release') + '\n#undef free\n'
    result += r'''
static short *worker_decode(BOOL adpcm,const unsigned char *source,unsigned long size,unsigned long channels,unsigned long *frames) {
 pthread_mutex_lock(&barrier_lock);entered_decode=1;pthread_cond_broadcast(&barrier_condition);
 while(pause_decode)pthread_cond_wait(&barrier_condition,&barrier_lock);
 pthread_mutex_unlock(&barrier_lock);return packet_decode(adpcm,source,size,channels,frames);
}
#define packet_decode worker_decode
#define pthread_cond_wait worker_wait
'''
    result += function(text, 'decoder_thread') + '\n#undef packet_decode\n#undef pthread_cond_wait\n'
    result += '\n'.join(function(text, name) for name in ['decoder_post', 'stream_process', 'stream_flush'])
    return result + '\n' + AFTER_FUNCTIONS


class PacketLifetimeTests(unittest.TestCase):
    def fixture(self, case='', sanitizer=False, baseline=False, binary=False):
        with tempfile.TemporaryDirectory(prefix='halo-packet-lifetime-') as directory:
            directory = Path(directory)
            source, exe = directory / 'lifetime.c', directory / 'lifetime'
            source.write_text(generated())
            command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('PACKET_POOL_TARGET', '')),
                       '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                       '-Wno-missing-field-initializers', '-ffp-contract=off', '-pthread',
                       '-I' + str(ROOT / 'port/wiiu/src'),
                       '-DPOOL_SOURCE="' + str(ROOT / 'port/wiiu/src/wiiu_packet_pool.c') + '"',
                       str(source), '-o', str(exe)]
            if baseline:
                command.append('-DPACKET_MEMORY_BASELINE')
            if sanitizer:
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            return subprocess.run([*shlex.split(os.environ.get('PACKET_POOL_RUNNER', '')), str(exe), case],
                                  capture_output=True, text=not binary, timeout=30)

    def test_completion_queue_exhaustion_and_copy_oom(self):
        result = self.fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_flush_during_decode(self):
        result = self.fixture('flush')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_release_during_decode(self):
        result = self.fixture('release')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reused_stream_address_rejects_old_serial(self):
        result = self.fixture('aba')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_decoded_pcm_bytes_match_the_heap_baseline(self):
        for case in ('bytes-adpcm', 'bytes-pcm'):
            reference = self.fixture(case, baseline=True, binary=True)
            pooled = self.fixture(case, binary=True)
            self.assertEqual(reference.returncode, 0, reference.stderr)
            self.assertEqual(pooled.returncode, 0, pooled.stderr)
            self.assertGreater(len(reference.stdout), 0)
            self.assertEqual(reference.stdout, pooled.stdout)

    @unittest.skipIf(bool(os.environ.get('PACKET_POOL_TARGET')), 'Sanitizers run on the host')
    def test_address_and_undefined_sanitizers(self):
        for case in ('', 'flush', 'release', 'aba'):
            result = self.fixture(case, sanitizer=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
