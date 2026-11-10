"""How far a client's copy of a hosted bot is from the host's, by how old the host's word on it was: the read-out of a two-machine run (tools/harness/netoverlap_two_machines.sh) that
HALO_BOTS_NET_LOG=1 (debug.bots_net_log) made log each bot's controls on the host ("bots: net act tick T player P ...") and each action the host relayed on the client
("bots: net relay update U player P now N ..."; N is the client's own game time when it came).

    python3 tools/harness/bots_net_stale.py positions HOST.log CLIENT.log      the mean distance between the host's copy of a player and the client's at the same tick,
                                                                               by the age of the latest relay the client had for that player (ticks of the client's clock
                                                                               past the update's number): 0-10, 10-30, 30-60, 60-120, 120-240, 240 and more
    python3 tools/harness/bots_net_stale.py actions HOST.log CLIENT.log        of the ticks the host built a bot's controls, how many the client was sent, whether
                                                                               the controls it got were the host's, and whether the buttons of the ticks the relay
                                                                               skipped came with the next (the history)

A client whose clock runs ahead of the host's (a host that is slower than the client, as a busy emulator is) runs a remote player on input that is seconds old: the older the
word, the larger the distance, and a bot that turns 12 to 25 degrees a tick is held further off than a player that walks straight. tools/net_compare.py compares at the same tick and
cannot tell the two apart; this does.
"""

import argparse
import bisect
import collections
import math
import re
import sys
from pathlib import Path

BUCKETS = [0, 10, 30, 60, 120, 240, 10 ** 9]
PLAYER = re.compile(r"player (\d+): \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)")
TICK = re.compile(r"network test: tick (\d+) ")
RELAY = re.compile(r"bots: net relay update (\d+) player (\d+) now (\d+) yaw (-?[\d.]+) pitch (-?[\d.]+) throttle (-?[\d.]+) (-?[\d.]+) trigger ([\d.]+) "
                   r"flags ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+)")
ACT = re.compile(r"bots: net act tick (\d+) player (\d+) yaw (-?[\d.]+) pitch (-?[\d.]+) throttle (-?[\d.]+) (-?[\d.]+) trigger ([\d.]+) flags ([0-9a-f]+)")


def positions(text):
    """{tick: {player: (x, y, z)}} of the network test's per-second lines."""
    found = {}
    for line in text.splitlines():
        match = TICK.search(line)
        if match:
            found[int(match.group(1))] = {int(m.group(1)): (float(m.group(2)), float(m.group(3)), float(m.group(4))) for m in PLAYER.finditer(line)}
    return found


def relays(text):
    """{player: [(client time, update)]} in the order the client had them."""
    found = collections.defaultdict(list)
    for line in text.splitlines():
        match = RELAY.search(line)
        if match:
            found[int(match.group(2))].append((int(match.group(3)), int(match.group(1))))
    return found


def staleness_table(host_text, client_text, bot_players=(1, 2, 3)):
    """{who: {bucket label: (samples, mean distance)}}: who is "bot" for the players named, else "human N"."""
    host, client, heard = positions(host_text), positions(client_text), relays(client_text)
    results = collections.defaultdict(lambda: collections.defaultdict(list))
    for tick in sorted(set(host) & set(client)):
        for player, where in host[tick].items():
            if player not in client[tick] or player not in heard:
                continue
            nows = [now for now, _ in heard[player]]
            index = bisect.bisect_right(nows, tick) - 1
            if index < 0:
                continue
            age = max(tick - heard[player][index][1], 0)
            bucket = next(k for k in range(len(BUCKETS) - 1) if BUCKETS[k] <= age < BUCKETS[k + 1])
            who = "bot" if player in bot_players else f"human {player}"
            results[who][bucket].append(math.dist(where, client[tick][player]))
    table = {}
    for who, buckets in results.items():
        table[who] = {(f"{BUCKETS[b]}-{BUCKETS[b + 1]}" if BUCKETS[b + 1] < 10 ** 9 else f"{BUCKETS[b]}+"): (len(v), sum(v) / len(v)) for b, v in sorted(buckets.items())}
    return table


def actions_table(host_text, client_text):
    """{player: dict} of what the host built for each bot and what the client was sent of it."""
    built = {}
    for line in host_text.splitlines():
        match = ACT.search(line)
        if match:
            built[(int(match.group(1)), int(match.group(2)))] = (float(match.group(3)), float(match.group(4)), float(match.group(5)), float(match.group(6)),
                                                                   int(match.group(8), 16))
    got = {}
    for line in client_text.splitlines():
        match = RELAY.search(line)
        if match:
            got.setdefault((int(match.group(1)), int(match.group(2))), (float(match.group(4)), float(match.group(5)), float(match.group(6)), float(match.group(7)),
                                                                           [int(match.group(i), 16) for i in (9, 10, 11, 12)]))
    table = {}
    for player in sorted({p for _, p in built}):
        ticks = sorted(t for t, p in built if p == player)
        relayed = [t for t in ticks if (t, player) in got]
        equal = 0
        for t in relayed:
            host_value, sent = built[(t, player)], got[(t, player)]
            turn = abs((sent[0] - host_value[0] + math.pi) % (2 * math.pi) - math.pi)
            if turn < 0.01 and abs(sent[2] - host_value[2]) < 0.02 and abs(sent[3] - host_value[3]) < 0.02:
                equal += 1
        pulses = collections.Counter()
        delivered = collections.Counter()
        for t in ticks:
            for bit in range(16):
                if built[(t, player)][4] >> bit & 1:
                    pulses[bit] += 1
                    if any((t + k, player) in got and got[(t + k, player)][4][k] >> bit & 1 for k in range(4)):
                        delivered[bit] += 1
        table[player] = {"built": len(ticks), "relayed": len(relayed), "equal": equal, "pulses": {bit: (delivered[bit], pulses[bit]) for bit in sorted(pulses)}}
    return table


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("what", choices=("positions", "actions"))
    parser.add_argument("host")
    parser.add_argument("client")
    args = parser.parse_args(argv)
    host, client = Path(args.host).read_text(errors="replace"), Path(args.client).read_text(errors="replace")
    if args.what == "positions":
        for who, row in sorted(staleness_table(host, client).items()):
            print(who, {label: f"{count} samples, mean {mean:.2f}" for label, (count, mean) in row.items()})
    else:
        for player, row in actions_table(host, client).items():
            print(f"player {player}: built {row['built']}, relayed {row['relayed']} ({100 * row['relayed'] / max(row['built'], 1):.0f}%), equal {row['equal']}; "
                  f"pulses delivered/built by bit {row['pulses']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
