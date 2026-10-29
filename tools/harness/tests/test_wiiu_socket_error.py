"""The Wii U socket error survives activity on another network thread."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read


@pytest.mark.parametrize("shared_error", [False, True], ids=["thread_local", "negative_control"])
def test_socket_error_is_thread_local(tmp_path, shared_error):
    source = read("port/wiiu/src/wiiu_posix_net.c")
    match = re.search(r"^static (?:__thread )?int last_error;", source, re.MULTILINE)
    assert match
    declaration = match.group()
    if shared_error:
        assert "__thread" in declaration
        declaration = declaration.replace("__thread ", "")
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define WSA"))
    code = """#include <errno.h>
#include <pthread.h>
#include <stdio.h>
""" + constants + "\n" + declaration + "\n" + "\n".join(
        function(source, name) for name in ("fail", "succeed", "posix_socket_last_error")) + """
static void *other_thread(void *ignored)
{
    (void)ignored;
    /* A successful call on this thread clears only this thread's error. */
    succeed(0);
    return 0;
}
int main(void)
{
    pthread_t thread;
    errno = EAGAIN;
    if (fail() != -1 || posix_socket_last_error() != WSAEWOULDBLOCK) return 2;
    if (pthread_create(&thread, 0, other_thread, 0)) return 3;
    if (pthread_join(thread, 0)) return 4;
    if (posix_socket_last_error() != WSAEWOULDBLOCK) return 1;
    puts("socket errors are independent");
    return 0;
}
"""
    test_source = tmp_path / "socket_error.c"
    test_source.write_text(code)
    executable = tmp_path / "socket_error"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    str(test_source), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    assert result.returncode == (1 if shared_error else 0), result.stdout + result.stderr
