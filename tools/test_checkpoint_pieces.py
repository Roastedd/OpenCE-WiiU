"""Exercise the production checkpoint write loop with short and failed writes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "port/linux/src/xbox_files.c").read_text()
start = source.index("BOOL platform_write_file_in_pieces(")
end = source.index("/* ReadFileEx/WriteFileEx", start)
fixture = r"""
#include <assert.h>
#include <stddef.h>
#include <time.h>
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef const void *LPCVOID;
#define TRUE 1
#define FALSE 0
struct platform_file { int unused; };
static struct platform_file file;
static int calls, pauses, fail_at;
static DWORD offset;
static const char *base;
static struct platform_file *file_from_handle(HANDLE h) { return h ? &file : NULL; }
static double file_io_now_ms(void) { return 0; }
static void file_io_note(struct platform_file *f, int w, DWORD n, double t) {
 (void)f; (void)w; (void)t; assert(n == offset);
}
static int test_sleep(const struct timespec *t, void *unused) {
 (void)unused; assert(t->tv_nsec == 1000000); pauses++; return 0;
}
#define nanosleep test_sleep
static BOOL write_at(struct platform_file *f, LPCVOID b, DWORD n, LPDWORD done, BOOL positioned, unsigned long long at) {
 (void)f; (void)positioned; (void)at;
 assert((const char *)b == base + offset);
 calls++;
 *done = calls == fail_at ? n / 2 : n;
 offset += *done;
 return calls != fail_at;
}
"""
main = r"""
int main(void) {
 char data[1000];
 DWORD done = 99;
 base = data;
 assert(platform_write_file_in_pieces(&file,data,1000,256,&done));
 assert(done==1000 && calls==4 && pauses==3);
 calls=pauses=offset=0; fail_at=2;
 assert(!platform_write_file_in_pieces(&file,data,1000,256,&done));
 assert(done==384 && calls==2 && pauses==1);
 calls=pauses=offset=0; fail_at=0;
 assert(platform_write_file_in_pieces(&file,data,0,0,&done));
 assert(done==0 && calls==0 && pauses==0);
 assert(platform_write_file_in_pieces(&file,data,1000,0,&done));
 assert(done==1000 && calls==1 && pauses==0);
 assert(!platform_write_file_in_pieces(NULL,data,1000,256,&done));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture + source[start:end] + main)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-DHALO_WIIU", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Checkpoint write-loop checks passed")
