"""Exercise Wii U audio ownership; set HALO_AUDIO_SANITIZERS=1 for ASan/UBSan."""
import subprocess
import sys
import tempfile
import unittest
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


class WiiUAudioQueueTest(unittest.TestCase):
    def test_release_and_decoder_ownership(self):
        backend = read("port/linux/src/dsound_sdl.c")
        start = backend.index("#define DECODER_JOBS 256")
        end = backend.index("static short *packet_decode", start)
        declarations = backend[start:end]
        functions = "\n".join(function(backend, name) for name in
                              ["stream_release", "packet_samples_give", "decoder_post"])
        fixture = r'''
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DSOUND_MIXER_YIELDS
#define STDMETHODCALLTYPE
#define MAXIMUM_STREAM_PACKETS 64
#define TRUE 1
#define FALSE 0
typedef int BOOL;
typedef unsigned long ULONG;
typedef struct { void *pvBuffer; unsigned long dwMaxSize; } XMEDIAPACKET;
typedef const XMEDIAPACKET *LPCXMEDIAPACKET;
struct voice_packet { short *samples; unsigned long frames, serial; BOOL ready; };
struct sdl_stream { unsigned long reference_count, channels; BOOL adpcm; struct sdl_stream *next;
  unsigned long packet_count, packet_head; struct voice_packet packets[MAXIMUM_STREAM_PACKETS]; };
typedef struct sdl_stream IDirectSoundStream;
static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_stream *streams, *mixer_walk_next;
static void mixer_lock_take(void) { pthread_mutex_lock(&mixer_lock); }
static struct sdl_stream *stream_from_interface(IDirectSoundStream *s) { return s; }
static void stream_flush(IDirectSoundStream *s) { (void)s; }
'''
        checks = r'''
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static struct sdl_stream *voice(void) {
  struct sdl_stream *s = calloc(1, sizeof(*s)); s->reference_count = 1; return s;
}
int main(void) {
  struct sdl_stream *a = voice(), *b = voice(), *c = voice();
  a->next = b; b->next = c; streams = a; mixer_walk_next = a;
  stream_release(a); CHECK(streams == b && mixer_walk_next == b);
  mixer_walk_next = c; stream_release(c); CHECK(b->next == NULL && mixer_walk_next == NULL);
  c = voice(); a = voice(); b->next = c; c->next = a; mixer_walk_next = c;
  stream_release(c); CHECK(b->next == a && mixer_walk_next == a);
  mixer_walk_next = b; stream_release(b); CHECK(streams == a && mixer_walk_next == a);

  unsigned char *original = malloc(4); memcpy(original, "ABCD", 4);
  XMEDIAPACKET input = { original, 4 }; a->channels = 2;
  CHECK(decoder_post(a, 17, &input));
  memset(original, 'X', 4); free(original);
  CHECK(decoder.count == 1 && decoder.jobs[0].serial == 17);
  CHECK(memcmp(decoder.jobs[0].source, "ABCD", 4) == 0);
  free(decoder.jobs[0].source);
  decoder.first = 1; decoder.count = 0;
  input.pvBuffer = NULL; input.dwMaxSize = 0;
  CHECK(decoder_post(a, 18, &input));
  CHECK(decoder.jobs[1].size == 0 && decoder.jobs[1].source != NULL);
  free(decoder.jobs[1].source);
  decoder.count = DECODER_JOBS;
  CHECK(!decoder_post(a, 19, &input));
  CHECK(decoder.count == DECODER_JOBS);

  a->packet_count = 1; a->packets[0].serial = 17;
  short *samples = malloc(2 * sizeof(short));
  packet_samples_give(a, 16, samples, 2);
  CHECK(!a->packets[0].ready && !a->packets[0].samples);
  samples = malloc(2 * sizeof(short));
  packet_samples_give(a, 17, samples, 2);
  CHECK(a->packets[0].ready && a->packets[0].samples == samples && a->packets[0].frames == 2);
  free(samples); mixer_walk_next = NULL; stream_release(a); CHECK(!streams);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="wiiu-audio-test-") as folder:
            source = Path(folder) / "audio_test.c"
            executable = Path(folder) / "audio_test"
            source.write_text(fixture + declarations + functions + checks)
            command = [os.environ.get("CC", "cc"), "-std=gnu99", "-Wall", "-Wextra", "-pthread"]
            if os.environ.get("HALO_AUDIO_SANITIZERS") == "1":
                command.append("-fsanitize=address,undefined")
            command += [str(source), "-o", str(executable)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
