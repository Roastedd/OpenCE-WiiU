"""A Wii U datagram receive that nsysnet refuses as too long (EMSGSIZE) is asked again shorter, and kept short after."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, mutated, read

FAULTS = {
    "kept": None,
    # (the receive as it was: asked once, at most 1472 bytes, and refused)
    "no_step_down": ("if (result >= 0 || errno != EMSGSIZE", "if (1 || result >= 0 || errno != EMSGSIZE"),
    # (the caller's length asked first, as the tunnel's 2048 was)
    "no_clamp": ("if (datagram && length > datagram_receive_limit)", "if (0 && length > datagram_receive_limit)"),
}


@pytest.mark.parametrize("fault", list(FAULTS))
def test_datagram_receive_steps_down_to_what_is_taken(tmp_path, fault):
    source = read("port/wiiu/src/wiiu_posix_net.c")
    if FAULTS[fault]:
        source = mutated(source, *FAULTS[fault])
    defines = "\n".join(line for line in source.splitlines() if line.startswith("#define DATAGRAM_RECEIVE_"))
    tables = "\n".join(re.findall(r"^static (?:int datagram_receive_limit|unsigned char datagram_sockets)\b[^;]*;", source, re.M))
    fixture = r'''
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#define LISTENERS 16
/* the console: a datagram socket's receive longer than its limit refused, the datagram kept; a stream's any length */
static int limit = 1200, waiting, asked[256], asked_count;
static void wiiu_log(const char *format, ...) { (void)format; errno = EINVAL; }
static int is_datagram[LISTENERS];
static int mock_receive(int socket, size_t length, socklen_t *address_length)
{
    if (asked_count < 256) asked[asked_count++] = (int)length;
    if (is_datagram[socket] && (int)length > limit) { if (address_length) *address_length = 0; errno = EMSGSIZE; return -1; }
    if (!waiting) { errno = EAGAIN; return -1; }
    if (address_length) *address_length = 16;
    { int size = waiting; waiting = 0; return size; }
}
#define recvfrom(socket, buffer, length, flags, address, address_length) ((void)(buffer), (void)(flags), (void)(address), mock_receive(socket, length, address_length))
#define recv(socket, buffer, length, flags) ((void)(buffer), (void)(flags), mock_receive(socket, length, NULL))
''' + defines + "\n" + tables + "\n"
    checks = r'''
int main(void) {
    static char buffer[4096];
    char address[16];
    socklen_t length;
    int result, index;

    datagram_sockets[5] = 1; is_datagram[5] = 1;
    /* the tunnel's 2048: 1472 first, then 8 bytes shorter until taken */
    waiting = 100; asked_count = 0; length = sizeof(address);
    result = receive(5, buffer, 2048, 0, 1, address, &length);
    if (result != 100 || length != 16) return 1;
    if (asked[0] != 1472) return 2;
    for (index = 1; index < asked_count; index++) if (asked[index] != asked[index - 1] - 8) return 3;
    if (asked[asked_count - 1] != 1200 || datagram_receive_limit != 1200) return 4;
    /* the next asks the limit at once */
    waiting = 60; asked_count = 0; length = sizeof(address);
    if (receive(5, buffer, 2048, 0, 1, address, &length) != 60 || asked_count != 1 || asked[0] != 1200) return 5;
    /* a shorter receive as it is asked */
    waiting = 60; asked_count = 0;
    if (receive(5, buffer, 512, 0, 0, NULL, NULL) != 60 || asked[0] != 512) return 6;
    /* nothing waiting: would block, as the call said */
    asked_count = 0;
    if (receive(5, buffer, 2048, 0, 1, address, &length) != -1 || errno != EAGAIN) return 7;
    /* a stream's length is the caller's */
    waiting = 3000; asked_count = 0;
    if (receive(6, buffer, 4096, 0, 0, NULL, NULL) != 3000 || asked[0] != 4096) return 8;
    /* refused down to 512: EMSGSIZE, after the log */
    limit = 100; waiting = 40; asked_count = 0; length = sizeof(address);
    if (receive(5, buffer, 2048, 0, 1, address, &length) != -1 || errno != EMSGSIZE) return 9;
    if (asked[asked_count - 1] != 512 || datagram_receive_limit != 512) return 10;
    return 0;
}
'''
    test_file = tmp_path / "datagram_receive.c"
    executable = tmp_path / "datagram_receive"
    test_file.write_text(fixture + function(source, "receive") + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test_file), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=False)
    if fault == "kept":
        assert result.returncode == 0
    elif fault == "no_step_down":
        assert result.returncode == 1
    else:
        assert result.returncode == 2
