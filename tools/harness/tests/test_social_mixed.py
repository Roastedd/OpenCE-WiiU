"""A 32-bit x86 machine and a big-endian PowerPC machine talk to each other through a relay (tests/social_peer.c, built twice): they befriend (a code typed on one, an accept
on the other), see each other online, one hosts a game and invites the other, a party is made and joined, chat goes both ways, and a friend is removed. Run in the
opence-ppc-be image, which has qemu-i386 and qemu-ppc; elsewhere the legs are missing and this is skipped. Pairs of the same type are the controls.

The relay is a Python program standing in for the brokers: it delivers each message a peer publishes to the peers that listen to its topic. (The MQTT code of p2p_signal.c is not in
this; the sealed bytes that cross are the engine's own.)"""
import os
import queue
import subprocess
import sys
import threading
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social, xplay

ROOT = Path(__file__).resolve().parents[3]


def leg_pair():
    legs = xplay.legs()
    return legs.get("le"), legs.get("be")


class Peer:
    def __init__(self, name, leg, seed, wall_hours, anchored, mutations=()):
        flags = [*leg.target_flags]
        with social.Leg(flags, leg.runner, leg.environment):
            self.executable = social.build("social_peer", mutations=mutations)
            self.command = [*leg.runner, str(self.executable)]
        self.process = subprocess.Popen(self.command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1,
                                        env={**os.environ, **leg.environment}, cwd=ROOT)
        self.name = name
        self.leg = leg
        self.topics = set()
        self.published = []
        self.send(f"init {name} {seed.hex()} {wall_hours} {anchored}")
        self.code = self.read("code").split()[1]

    def send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def read(self, expect):
        """lines until one starting with expect; the others are returned through self.published"""
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise AssertionError(f"{self.name} ended: {self.process.stderr.read()}")
            line = line.rstrip("\n")
            if line.startswith(expect):
                return line
            if line.startswith("pub "):
                self.published.append(line.split()[1:])
            elif line.startswith("sub"):
                self.topics = set(line.split()[1:])

    def close(self):
        try:
            self.send("quit")
            self.process.wait(timeout=20)
        except Exception:
            self.process.kill()


def run_pair(first_leg, second_leg, second_mutations=()):
    a = Peer("Alpha", first_leg, bytes(range(1, 33)), 5, 1)
    b = Peer("Bravo", second_leg, bytes(range(101, 133)), -9, 0, second_mutations)
    peers = [a, b]
    for peer in peers:
        peer.published = []

    def advance(ms=100):
        for peer in peers:
            peer.send(f"tick {ms}")
        for peer in peers:
            peer.read("ticked")
        # what was published is delivered to the other, who listens to the topic
        for peer, other in ((a, b), (b, a)):
            batch, peer.published = peer.published, []
            for topic, hex_text in batch:
                if topic in other.topics:
                    other.send(f"heard {topic} {hex_text}")

    def run(seconds):
        for _ in range(seconds * 10):
            advance()

    def dump(peer):
        peer.send("dump")
        return peer.read("dump")

    try:
        run(17)
        a.send(f"do add {b.code}")
        run(2)
        assert "requests 1" in dump(b), dump(b)
        b.send("do accept 0")
        run(15)
        da, db = dump(a), dump(b)
        assert "friends 1" in da and "online=1" in da and "Bravo" in da, da
        assert "friends 1" in db and "online=1" in db and "Alpha" in db, db
        # the platform and the version crossed intact
        assert "version=41" in da and "version=41" in db
        a.send("do place bloodgulch 3 77")
        run(6)
        db = dump(b)
        assert "joinable=1" in db and "map=bloodgulch" in db and "players=3" in db, db
        a.send("do invite 0")
        run(2)
        db = dump(b)
        assert "invite kind=1 from=Alpha map=bloodgulch version=41" in db, db
        a.send("do party make")
        run(11)
        a.send("do party invite 0")
        run(2)
        b.send("do party join")
        run(4)
        da, db = dump(a), dump(b)
        assert "party 2 leader Alpha" in da and "party 2 leader Alpha" in db, (da, db)
        b.send("do party chat hello from b")
        a.send("do party chat hello from a")
        run(2)
        da, db = dump(a), dump(b)
        assert "chat Bravo: hello from b" in da and "chat Alpha: hello from a" in da, da
        assert "chat Bravo: hello from b" in db and "chat Alpha: hello from a" in db, db
        a.send("do party transfer 1")
        run(3)
        da, db = dump(a), dump(b)
        assert "leader Bravo" in da and "leader Bravo" in db, (da, db)
        b.send("do party leave")
        run(2)
        assert "party 1" in dump(a)
        a.send("do remove 0")
        run(10)
        assert "friends 0" in dump(a) and "friends 0" in dump(b)
    finally:
        for peer in peers:
            peer.close()


@pytest.mark.parametrize("first,second", [("le", "be"), ("be", "le"), ("le", "le"), ("be", "be")])
def test_pair(first, second):
    legs = xplay.legs()
    if first not in legs or second not in legs:
        pytest.skip("needs the opence-ppc-be image: qemu-i386 and qemu-ppc")
    run_pair(legs[first], legs[second])


@pytest.mark.parametrize("fault", [
    # a 16-bit field written in the machine's order on one side
    ("social_codec.c", "void social_put16(struct social_writer *writer, unsigned int value)\n{\n	social_put8(writer, value >> 8);\n	social_put8(writer, value);\n}",
     "void social_put16(struct social_writer *writer, unsigned int value)\n{\n	social_put8(writer, value);\n	social_put8(writer, value >> 8);\n}"),
    # the big-endian side reads a 16-bit field the other way
    ("social_codec.c", "	unsigned int high = social_get8(reader);\n\n	return high << 8 | social_get8(reader);", "	unsigned int low = social_get8(reader);\n\n	return low | social_get8(reader) << 8;"),
])
def test_negative_control(fault):
    """one side with a byte order fault: the pair does not manage the scenario"""
    legs = xplay.legs()
    if "le" not in legs or "be" not in legs:
        pytest.skip("needs the opence-ppc-be image: qemu-i386 and qemu-ppc")
    with pytest.raises((AssertionError, BrokenPipeError, ValueError)):
        run_pair(legs["le"], legs["be"], (fault,))
