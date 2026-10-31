"""Internet play's inboxes in the process (network.tunnel_in_process, p2p.c): a game socket's datagrams from peers come out
in order with their peers' addresses, round the ring's end, a full ring refuses what does not fit, and a short buffer
takes a datagram's start."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

CASES = ["order", "wrap", "full", "truncate"]


def generated(fault=None):
    source = read("port/linux/src/p2p.c")
    code = "\n".join(function(source, name) for name in ["inbox_write", "inbox_read", "inbox_put", "inbox_take"])
    if fault:
        code = mutated(code, *fault)
    header = f"#define INBOX_HEADER_SIZE {constant(source, 'INBOX_HEADER_SIZE')}\n"
    return (("under_test.inc", header + structure(source, "inbox") + "\n" + code),)


@pytest.mark.parametrize("case", CASES)
def test_inbox(case):
    status, output = run(build("p2p_inbox", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    # (no room asked for: a full ring is written over)
    (("size > INBOX_SIZE - INBOX_HEADER_SIZE - inbox->used", "size > INBOX_SIZE"), "full"),
    # (the ring's end not come round: what is cut by it is lost)
    (("memcpy(inbox->bytes, (const unsigned char *)data + first, (size_t)(size - first));", ""), "wrap"),
    # (a datagram taken without its header: the next is read from the wrong place)
    (("inbox->start = (inbox->start + INBOX_HEADER_SIZE + size) % INBOX_SIZE;",
      "inbox->start = (inbox->start + size) % INBOX_SIZE;"), "order"),
    # (a short buffer filled past its length)
    (("length < 0 ? 0 : size < length ? size : length", "size"), "truncate"),
])
def test_negative_control(fault, case):
    status, output = run(build("p2p_inbox", generated(fault)), case)
    assert status == CHECK_FAILED, output
