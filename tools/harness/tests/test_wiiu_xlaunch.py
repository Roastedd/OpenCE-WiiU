"""XLaunchNewImage must hand Wii U shutdown to ProcUI, not call exit()."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


class XLaunchTest(unittest.TestCase):
    def check_target(self, wiiu):
        source = function(read("port/linux/src/xbox_xapi.c"), "XLaunchNewImageA")
        fixture = r'''
#include <setjmp.h>
#include <stdlib.h>
#include <time.h>
typedef unsigned long DWORD;
typedef const char *LPCSTR;
typedef void *PLAUNCH_DATA;
#define WINAPI
static jmp_buf finished;
static int requested, gates, sleeps, exited;
static void platform_log(const char *format, ...) { (void)format; }
void platform_request_quit(void) { requested++; }
void wiiu_foreground_gate(int game) {
    if (game != 1 || requested != 1) longjmp(finished, 3);
    gates++;
    if (gates == 2) longjmp(finished, 1);
}
static int test_sleep(const struct timespec *duration, struct timespec *remaining) {
    (void)remaining;
    if (duration->tv_sec != 0 || duration->tv_nsec != 10000000L) longjmp(finished, 4);
    sleeps++;
    return 0;
}
#define nanosleep test_sleep
__attribute__((noreturn)) static void test_exit(int status) {
    if (status != EXIT_SUCCESS) longjmp(finished, 5);
    exited++;
    longjmp(finished, 1);
}
#define exit test_exit
'''
        main = r'''
int main(void) {
    int result = setjmp(finished);
    if (!result) XLaunchNewImageA(NULL, NULL);
    if (result != 1) return result;
#ifdef HALO_WIIU
    return !(requested == 1 && gates == 2 && sleeps == 1 && exited == 0);
#else
    return !(exited == 1 && requested == 0 && gates == 0 && sleeps == 0);
#endif
}
'''
        with tempfile.TemporaryDirectory(prefix="wiiu-xlaunch-") as folder:
            source_file = Path(folder) / "test.c"
            executable = Path(folder) / "test"
            source_file.write_text(fixture + source + main)
            command = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-function", "-Werror"]
            if wiiu:
                command.append("-DHALO_WIIU")
            subprocess.run([*command, str(source_file), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_wiiu_requests_shutdown_and_parks(self):
        self.check_target(True)

    def test_other_platforms_keep_exit_behavior(self):
        self.check_target(False)


if __name__ == "__main__":
    unittest.main()
