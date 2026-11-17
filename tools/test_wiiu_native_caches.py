"""Compile production cache routines against a memory-watch test fixture."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
files = (root / "port/linux/src/xbox_files.c").read_text()
gx = (root / "port/wiiu/src/wiiu_gx2.c").read_text()
common = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
static void __attribute__((unused)) platform_log(const char *format, ...) { (void)format; }
typedef int BOOL;
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
typedef const void *LPCVOID;
#define TRUE 1
#define FALSE 0
"""
range_source = gx[gx.index("static struct\n{\n\tuint32_t address, count, lowest"):gx.index("static void cache_flush(void)")]
range_test = common + r"""
static struct { uint32_t window_base, window_size; } blocks={0x1000,0x1000};
static unsigned long generation=1;
static unsigned scans;
static unsigned long memory_watch_serial(void) { return generation; }
static int copy_is_current(uint32_t address,uint32_t size,unsigned long serial) {
 (void)address;(void)size; return serial==generation;
}
static void wiiu_index_range(const uint16_t *list,uint32_t count,uint32_t *lo,uint32_t *hi) {
 (void)list;(void)count; scans++; *lo=generation; *hi=generation+3;
}
""" + range_source + r"""
int main(void) {
 uint32_t lo,hi;
 const uint16_t *p=(void *)(uintptr_t)0x1100;
 cached_index_range(p,10,&lo,&hi);
 assert(scans==1 && lo==1 && hi==4);
 cached_index_range(p,10,&lo,&hi);
 assert(scans==1);
 generation++;
 cached_index_range(p,10,&lo,&hi);
 assert(scans==2 && lo==2 && hi==5);
 cached_index_range(p,11,&lo,&hi);
 assert(scans==3);
 cached_index_range((void *)(uintptr_t)0x3000,10,&lo,&hi);
 cached_index_range((void *)(uintptr_t)0x3000,10,&lo,&hi);
 assert(scans==5);
 memset(index_ranges,0,sizeof(index_ranges));
 cached_index_range(p,10,&lo,&hi);
 assert(scans==6);
 cached_index_range(p,0,&lo,&hi);
 cached_index_range(p,0,&lo,&hi);
 assert(scans==8);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    for name, source in (("range", range_test),):
        path = Path(directory)
        (path / f"{name}.c").write_text(source)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-DHALO_WIIU",
                        str(path / f"{name}.c"), "-pthread", "-o", str(path / name)], check=True)
        subprocess.run([str(path / name)], check=True)
        print(f"{name} cache checks passed")
