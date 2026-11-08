"""The friends layer (port/linux/src/social_*.c) on a fake network of brokers: the friend code, the sealed envelope, the replay window, presence, the budgets, the
abuse it must survive, and the bytes on the wire against one golden file on both byte orders (run it in the opence-ppc-be image: tools/ppc-be/run.sh harness
tools/harness/tests/test_social.py). tests/social_check.c has the cases; each is a test here, and the negative controls break the sources and show a named case
fail."""
import hashlib
import hmac
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social
from ed25519_ref import public_key

GOLDEN = Path(__file__).resolve().parent / "social_golden"

CASES = ["code-check", "code-forgiving", "envelope", "window", "presence-codec", "presence-text", "message-codecs", "fuzz-decoders", "presence-basic",
         "clock-offsets", "offline-and-timeout", "details", "forgery-and-replay", "abuse", "queue-and-bucket", "subscriptions", "fifty-friends"]

DIGITS = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"


def reference_code(key):
    """the friend code of a public key, worked out here from the design (social.md 3.1), not from the C"""
    digest = hashlib.sha256(b"hceu-friend-code-1" + key).digest()
    bits = "".join(f"{byte:08b}" for byte in digest[:8])[:60]
    symbols = [int(bits[i:i + 5], 2) for i in range(0, 60, 5)]
    value = 0
    for symbol in symbols:
        value = ((value ^ symbol) << 1 & 31) ^ (5 if (value ^ symbol) & 16 else 0)
    symbols.append(value)
    text = "".join(DIGITS[s] for s in symbols)
    return "-".join([text[0:4], text[4:8], text[8:12], text[12:13]])


def reference_derivations(hex_prefix=""):
    """the inbox and circle topics and seals, from the design"""
    circle = bytes((index * 7 + 3) & 255 for index in range(32))

    def hm(key, message):
        return hmac.new(key, message, hashlib.sha256).digest()

    out = {}
    seed_b = bytes((0xF0 - index) & 255 for index in range(32))
    key_b = public_key(seed_b)
    digest = hashlib.sha256(b"hceu-friend-code-1" + key_b).digest()
    code_bytes = digest[:7] + bytes([digest[7] & 0xF0])
    inbox_secret = hm(code_bytes, b"hceu-f/inbox")
    out["inbox-topic-b"] = "hceu/3/f/i/" + hm(inbox_secret, b"topic")[:16].hex()
    out["inbox-seal-b"] = hm(inbox_secret, b"seal").hex()
    out["circle-topic"] = "hceu/3/f/p/" + hm(circle, b"topic")[:16].hex()
    out["circle-seal"] = hm(circle, b"seal").hex()
    out["key-b"] = key_b.hex()
    out["code-b"] = reference_code(key_b)
    seed_a = bytes(index + 1 for index in range(32))
    out["key-a"] = public_key(seed_a).hex()
    out["code-a"] = reference_code(public_key(seed_a))
    return out


@pytest.fixture(scope="module")
def driver():
    return social.build("social_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


def test_code_agrees_with_an_independent_implementation(driver):
    """the friend code of 40 seeds, in Python (Ed25519 and SHA-256 from the standard library and tools/ed25519_ref.py) and in the C"""
    import random

    generator = random.Random(7)
    for _ in range(40):
        seed = bytes(generator.randrange(256) for _ in range(32))
        status, output, errors = social.run(driver, "code-vectors", seed.hex())
        assert status == 0, errors
        assert output.strip() == reference_code(public_key(seed))


def parse_golden(text):
    result = {}
    for line in text.splitlines():
        label, _, value = line.partition(" ")
        result[label] = value
    return result


def test_wire_bytes_equal_the_golden_file(driver):
    """every message type from fixed identities and nonces is the same bytes on every machine: this file is compared on x86 and on big-endian PowerPC
    (tools/ppc-be/run.sh harness) against one golden file. HALO_SOCIAL_UPDATE=1 rewrites it (from a little-endian run only)."""
    import os

    status, output, errors = social.run(driver, "golden")
    assert status == 0, errors
    path = GOLDEN / "wire.txt"
    if os.environ.get("HALO_SOCIAL_UPDATE") == "1" and not social.big_endian():
        path.write_text(output)
    assert output == path.read_text(), "the wire bytes changed (or differ on this byte order)"


def test_topics_and_keys_agree_with_the_design(driver):
    """the inbox and circle topics and seals are HMAC-SHA256 chains of the design; here they are worked out independently"""
    status, output, errors = social.run(driver, "golden")
    assert status == 0, errors
    got = parse_golden(output)
    expected = reference_derivations()
    for label, value in expected.items():
        assert got[label] == value, label


def test_wire_layout_by_hand(driver):
    """a few fields of the golden messages read by position, so that the golden file cannot drift to something else unnoticed"""
    status, output, errors = social.run(driver, "golden")
    got = parse_golden(output)
    presence = bytes.fromhex(got["presence-plain"])
    # type, generation, sequence, big-endian
    assert presence[0] == ord("P")
    assert presence[1:5] == bytes([1, 2, 3, 4]) and presence[5:9] == bytes([0x0A, 0x0B, 0x0C, 0x0D])
    assert presence[9] == 3 and presence[13:15] == (41).to_bytes(2, "big")
    hello = bytes.fromhex(got["hello-plain"])
    assert hello[0] == ord("H") and hello[9] == 5 and hello[10:42] == bytes((index * 7 + 3) & 255 for index in range(32)) and hello[42] == 2
    wire = bytes.fromhex(got["hello-wire"])
    assert wire[0] == ord("D") and wire[1] == 1 and wire[2:10] == bytes.fromhex(got["tag-a-to-b"]) and len(wire) == 10 + 12 + len(hello) + 16
    wire = bytes.fromhex(got["presence-wire"])
    assert wire[0] == ord("P") and wire[1] == 1 and wire[2:14] == bytes(0xA0 + index for index in range(12))
    invite = bytes.fromhex(got["invite-game-plain"])
    assert invite[9] == 1 and invite[42:44] == (0x0129).to_bytes(2, "big") and invite[44:47] == bytes([3, 16, 2])
    assert got["window"] == "01020304 0a0b0c0d 00000002"


# ---- negative controls: a fault in the source, a named case that must fail

FAULTS = [
    # the check symbol of the code ignored
    ("code-check", "social_codec.c", "	if (value != 0)\n		return SOCIAL_CODE_BAD_CHECK;", "	if (0)\n		return SOCIAL_CODE_BAD_CHECK;"),
    # a neighbour swap no longer caught: the loop forgets the multiplication
    ("code-check", "social_codec.c", "return ((value << 1) & 31) ^ (value & 16 ? 5 : 0);", "return (value & 31);"),
    # the topic not in the envelope's additional data
    ("envelope", "social_codec.c", "	memcpy(additional + header, topic, (size_t)topic_size);\n	memcpy(wire + header, nonce, SOCIAL_NONCE_SIZE);\n	p2p_aead_seal(key, nonce, additional, header + topic_size,",
     "	memcpy(additional + header, topic, (size_t)topic_size);\n	memcpy(wire + header, nonce, SOCIAL_NONCE_SIZE);\n	p2p_aead_seal(key, nonce, additional, header,"),
    # the window lets a repeat of the highest through
    ("window", "social_codec.c", "	if (difference == 0 || difference > 32)\n		return 0;", "	if (difference > 32)\n		return 0;"),
    # the window marks nothing below the highest
    ("window", "social_codec.c", "	if (difference >= 1 && difference <= 32)\n		window->bitmap |= 1u << (difference - 1);", "	(void)difference;"),
    # the window forgets a higher generation
    ("window", "social_codec.c", "	if (generation < window->generation)\n		return 0;\n	if (sequence > window->sequence)\n		return 1;", "	if (sequence > window->sequence)\n		return 1;"),
    # a presence text not cleaned
    ("presence-text", "social_codec.c", "		text_safe_text(text, (short)size, raw, (short)length, (short)maximum);", "		memcpy(text, raw, length), text[length] = 0;"),
    # the presence signature never checked
    ("forgery-and-replay", "social_codec.c", "	return social_verify(\"hceu-f/presence\", hash, SOCIAL_HASH_SIZE, key, plain, signed_size, signature);", "	return 1;"),
    # the window not marked after a presence
    ("forgery-and-replay", "social_core.c", "	social_window_mark(&friend->window, inner->generation, inner->sequence);\n	engine->statistics.accepted++;", "	engine->statistics.accepted++;"),
    # no dedupe: the same message through four brokers is taken four times
    ("presence-basic", "social_core.c", "	if (seen_before(engine, payload, size, id))\n	{\n		engine->statistics.dropped_seen++;\n		return;\n	}", "	seen_before(engine, payload, size, id);"),
    # a friend never offline
    ("offline-and-timeout", "social_core.c", "		if (friend->heard && social_elapsed(engine, friend->heard_time, SOCIAL_OFFLINE_TIME))", "		if (0)"),
    # the signature budget gone
    ("abuse", "social_core.c", "	if (engine->verify_tokens <= 0)\n		return 0;\n	engine->verify_tokens--;\n	return 1;", "	return 1;"),
    # the open rate gone
    ("abuse", "social_core.c", "	return ++engine->open_count <= SOCIAL_OPEN_RATE;", "	return 1;"),
    # no limit on one friend's messages
    ("abuse", "social_core.c", "	return ++friend->rate_count <= SOCIAL_FRIEND_RATE;", "	return 1;"),
    # the waiting presences not bounded by age or number: the queue's guard
    ("abuse", "social_core.c", "	if (engine->pending_count == SOCIAL_MAX_PENDING)\n	{\n		engine->statistics.dropped_verify_budget++;\n		return;\n	}", "	if (0)\n	{\n		return;\n	}"),
    # publishing without the bucket
    ("queue-and-bucket", "social_core.c", "else if (engine->send_tokens <= 0 || !engine->env.publish(", "else if (!engine->env.publish("),
    # the queue unbounded by age
    ("queue-and-bucket", "social_core.c", "		if (social_elapsed(engine, queued->time, SOCIAL_QUEUE_LIFE))", "		if (0)"),
    # presence ignoring the share setting: a hidden machine publishes
    ("offline-and-timeout", "social_core.c", "	if (!engine->share_status)\n	{\n		/* appear offline", "	if (0)\n	{\n		/* appear offline"),
    # details given away at basic
    ("details", "social_core.c", "	if (full || joinable)\n	{\n		presence->flags |= SOCIAL_PRESENCE_DETAILS;", "	if (1)\n	{\n		presence->flags |= SOCIAL_PRESENCE_DETAILS;"),
    # a solo game joinable
    ("details", "social_core.c", "	if (clean.activity != SOCIAL_LOBBY && clean.activity != SOCIAL_GAME)\n		clean.joinable = 0;", "	(void)clean.activity;"),
    # the topics never refreshed when friends change
    ("subscriptions", "social_core.c", "		engine->topics_dirty = 1;\n	}\n	if (acknowledged)", "	}\n	if (acknowledged)"),
    # a generation chosen without the clock's floor
    ("clock-offsets", "social_core.c", "	engine->generation = engine->stored_generation + 1 > minutes ? engine->stored_generation + 1 : minutes;", "	engine->generation = engine->stored_generation + 1;\n	(void)minutes;"),
    # no list cap
    ("fifty-friends", "social_core.c", "		if (index == SOCIAL_MAX_FRIENDS)\n			return NULL;", "		if (index == SOCIAL_MAX_FRIENDS)\n			index = 0;"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
