"""A failing poll keeps its error across diagnostics and the retry delay."""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


@pytest.mark.parametrize("lose_errno", [False, True], ids=["preserved", "negative_control"])
def test_poll_error_survives_logging_and_sleep(tmp_path, lose_errno):
    source = read("port/wiiu/src/wiiu_posix_net.c")
    if lose_errno:
        source = source.replace("\t\terrno = poll_error;\n", "", 1)
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define WSA"))
    fixture = r'''
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
typedef long posix_long;
typedef unsigned long long OSTime;
static __thread int last_error;
static int requested_error, invalid_descriptor;
static int sockets_enabled(void) { return 1; }
static int wiiu_network_allowed(void) { return 1; }
static OSTime OSGetSystemTime(void) { return 10000; }
static OSTime OSSecondsToTicks(unsigned s) { return s * 1000; }
static OSTime OSMillisecondsToTicks(unsigned s) { return s; }
static void OSSleepTicks(OSTime ticks) { (void)ticks; errno = EINVAL; }
static void wiiu_log(const char *format, ...) { (void)format; errno = EINVAL; }
static int mock_poll(struct pollfd *fds, nfds_t count, int timeout) {
    (void)timeout;
    if (invalid_descriptor && count) { fds[0].revents = POLLNVAL; return 1; }
    errno = requested_error;
    return -1;
}
#define poll mock_poll
#define LISTENERS 1
static struct { int listening, quiet_ms; uint32_t quiet_until; } listeners[LISTENERS];
static uint32_t clock_ms(void) { return 0; }
'''
    funcs = "\n".join(function(source, name) for name in ("fail", "posix_socket_select"))
    checks = r'''
int main(void) {
    int fd = 12, count;
    requested_error = ECANCELED; count = 1;
    if (posix_socket_select(&fd, &count, 0, 0, 0, 0, 0, 5000, 0) != -1 || last_error != WSAENETDOWN) return 1;
    requested_error = EAGAIN; count = 1;
    if (posix_socket_select(&fd, &count, 0, 0, 0, 0, 0, 5000, 0) != -1 || last_error != WSAEWOULDBLOCK) return 2;
    invalid_descriptor = 1; count = 1;
    if (posix_socket_select(&fd, &count, 0, 0, 0, 0, 0, 5000, 0) != -1 || last_error != WSAEBADF) return 3;
    return 0;
}
'''
    test_file = tmp_path / "poll_error.c"
    executable = tmp_path / "poll_error"
    test_file.write_text(fixture + constants + "\n" + funcs + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test_file), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=False)
    assert result.returncode == (1 if lose_errno else 0)
