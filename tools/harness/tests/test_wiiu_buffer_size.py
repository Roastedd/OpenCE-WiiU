"""A Wii U socket buffer size that nsysnet refuses is asked again smaller, from 64 KB, and the call fails only when none is taken."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, mutated, read


@pytest.mark.parametrize("step_down", [True, False], ids=["steps_down", "negative_control"])
def test_buffer_size_steps_down(tmp_path, step_down):
    source = read("port/wiiu/src/wiiu_posix_net.c")
    if not step_down:
        source = mutated(source, "result < 0 && tried >= BUFFER_SIZE_LEAST", "0 && tried >= BUFFER_SIZE_LEAST")
    defines = "\n".join(line for line in source.splitlines() if line.startswith("#define BUFFER_SIZE_"))
    fixture = r'''
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
/* the console: a size over its limit refused, with nothing set */
static int limit = 32768, sizes[16], set_count, current = 4096;
static void wiiu_log(const char *format, ...) { (void)format; errno = ENOENT; }
static int mock_setsockopt(int size)
{
    if (set_count < 16) sizes[set_count++] = size;
    if (size > limit) { errno = EINVAL; return -1; }
    current = size;
    return 0;
}
#define setsockopt(socket, level, name, value, length) ((void)(socket), (void)(level), (void)(name), (void)(length), mock_setsockopt(*(const int *)(value)))
#define getsockopt(socket, level, name, value, length) ((void)(socket), (void)(level), (void)(name), (void)(length), *(int *)(value) = current, 0)
''' + defines + "\n"
    checks = r'''
int main(void) {
    /* 1 MB: refused, then 64 KB refused, 32 KB taken */
    if (set_buffer_size(3, SO_RCVBUF, 1 << 20) != 0 || current != 32768) return 1;
    if (set_count != 3 || sizes[0] != 1 << 20 || sizes[1] != 65536 || sizes[2] != 32768) return 2;
    /* a size taken as it is asked */
    set_count = 0;
    if (set_buffer_size(3, SO_SNDBUF, 16384) != 0 || set_count != 1 || current != 16384) return 3;
    /* none taken: the call's own error, after the log */
    limit = 4000; set_count = 0;
    if (set_buffer_size(3, SO_RCVBUF, 1 << 20) != -1 || errno != EINVAL) return 4;
    if (sizes[set_count - 1] != 8192) return 5;
    /* a small size refused is not made larger */
    set_count = 0;
    if (set_buffer_size(3, SO_RCVBUF, 6000) != -1 || set_count != 1) return 6;
    return 0;
}
'''
    test_file = tmp_path / "buffer_size.c"
    executable = tmp_path / "buffer_size"
    test_file.write_text(fixture + function(source, "set_buffer_size") + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test_file), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=False)
    assert result.returncode == (0 if step_down else 1)
