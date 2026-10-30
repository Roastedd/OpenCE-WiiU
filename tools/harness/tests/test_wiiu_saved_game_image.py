"""A saved game is read into an image and checked before it is taken. The Wii U
keeps that image for the session (made at start), so a revert late in a session
does not need a new 20 MiB block; the other builds make it each time. A failed
read says which step failed."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read

SOURCE = "source/saved games/game_state_xbox.c"


def image_helpers(text):
    start = text.index("/* port: the image a saved game is read into and checked")
    return text[start:]


class SavedGameImageTest(unittest.TestCase):
    def run_fixture(self, wiiu):
        text = read(SOURCE)
        reader = function(text, "game_state_read_from_file")
        fixture = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define FILE_BEGIN 0
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
static struct {
    int buffer_allocated, file_open, file_valid_for_read;
    void *handle;
    long buffer_size;
} xbox_game_state_globals = { 1, 1, 1, (void *)(uintptr_t)1, 4096 };
static int recover_saved_games_hack;
static char temporary[256], failure[256];
static unsigned error_value, allocations, frees, seeks, reads, accepts, faults;
static int fail_allocation, fail_seek, fail_read, short_read, refuse;
static void *test_malloc(size_t size) {
    allocations++;
    return fail_allocation ? NULL : calloc(1, size);
}
/* as the game's free (debug_memory.c), a null pointer is an error */
static void test_free(void *pointer) { if (!pointer) abort(); frees++; free(pointer); }
static unsigned SetFilePointer(void *file, long distance, void *high, unsigned mode) {
    (void)file; (void)distance; (void)high; (void)mode;
    seeks++;
    error_value = fail_seek ? 8 : 0;
    return fail_seek ? INVALID_SET_FILE_POINTER : 0;
}
static int ReadFile(void *file, void *image, unsigned long count, unsigned long *done, void *overlapped) {
    (void)file; (void)overlapped;
    assert(image);
    reads++;
    *done = short_read ? count - 1 : count;
    if (fail_read) error_value = 8;
    return !fail_read;
}
static boolean game_state_image_accept(void *image, long size) {
    assert(image && size == xbox_game_state_globals.buffer_size);
    accepts++;
    return !refuse;
}
static unsigned GetLastError(void) { return error_value; }
static char *csprintf(char *out, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(out, 256, format, arguments);
    va_end(arguments);
    return out;
}
#define match_assert(file, line, condition) assert(condition)
#define match_vassert(file, line, condition, message) do { if (!(condition)) { faults++; snprintf(failure, sizeof(failure), "%s", message); } } while (0)
static void *game_state_image_get(unsigned long size);
static void game_state_image_release(void *image);
#define malloc test_malloc
#define free test_free
'''
        main = r'''
#undef malloc
#undef free
static void reset(void) {
    error_value = 0;
    seeks = reads = accepts = faults = 0;
    fail_allocation = fail_seek = fail_read = short_read = refuse = 0;
    failure[0] = 0;
}
static int expect_step(int step) {
    char text[32];
    snprintf(text, sizeof(text), "step %d)", step);
    return faults == 1 && strstr(failure, text) != NULL;
}
int main(void) {
    int i;
    /* three good reads */
    for (i = 0; i < 3; i++) {
        reset();
        if (!game_state_read_from_file() || faults || accepts != 1) return 10 + i;
    }
#ifdef HALO_WIIU
    /* one image, made once and kept */
    if (allocations != 1 || frees != 0) return 20;
#else
    if (allocations != 3 || frees != 3) return 21;
#endif
    reset(); fail_seek = 1;
    if (game_state_read_from_file() || !expect_step(2) || reads) return 30;
    reset(); fail_read = 1;
    if (game_state_read_from_file() || !expect_step(3) || accepts) return 31;
    reset(); short_read = 1;
    if (game_state_read_from_file() || !expect_step(4) || accepts) return 32;
    reset(); refuse = 1;
    if (game_state_read_from_file() || !expect_step(5) || accepts != 1) return 33;
#ifndef HALO_WIIU
    /* the image is made for each read here: a failed allocation is step 1 */
    reset(); fail_allocation = 1;
    if (game_state_read_from_file() || !expect_step(1) || seeks) return 34;
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="saved-game-image-") as folder:
            source_file = Path(folder) / "test.c"
            executable = Path(folder) / "test"
            source_file.write_text(fixture + reader + "\n" + image_helpers(text) + main)
            command = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-function",
                       "-Wno-unused-parameter", "-Wno-sign-compare", "-Werror", "-fsanitize=address,undefined"]
            if wiiu:
                command.append("-DHALO_WIIU")
            subprocess.run([*command, str(source_file), "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)])
            self.assertEqual(result.returncode, 0)

    def test_wiiu_keeps_one_image(self):
        self.run_fixture(True)

    def test_other_platforms_make_it_each_time(self):
        self.run_fixture(False)


if __name__ == "__main__":
    unittest.main()
