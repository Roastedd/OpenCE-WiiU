"""The friends layer under abuse and at scale (tests/social_hard_check.c): the whole receiving path fed messages sealed with the keys it accepts around altered bytes (also
under the address and undefined-behaviour sanitisers: SOCIAL_SANITIZE=1), the engine's whole conversation of a scenario as bytes against one golden file on every byte order
(friends, an invite, a party, chat, a hand-over, removing a friend), and eight simulated people on brokers that lose, repeat and reorder messages and go down in turn, with a
malicious peer among them."""
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

GOLDEN = Path(__file__).resolve().parent / "social_golden"


@pytest.fixture(scope="module")
def driver():
    return social.build("social_hard_check")


def iterations():
    # (qemu runs a tenth as fast)
    return str(int(os.environ.get("SOCIAL_FUZZ", "20000" if not social.runner() else "3000")))


def test_fuzz_pipeline(driver):
    status, output, errors = social.run(driver, "fuzz-pipeline", iterations())
    assert status == 0, errors + output


def test_sim(driver):
    status, output, errors = social.run(driver, "sim", "12" if not social.runner() else "6")
    assert status == 0, errors + output


def test_engine_conversation_equals_the_golden_file(driver):
    """what two engines say to each other, as bytes, from fixed seeds: the same on x86 and on big-endian PowerPC. HALO_SOCIAL_UPDATE=1 rewrites it (little-endian only)."""
    status, output, errors = social.run(driver, "golden-engine")
    assert status == 0, errors
    path = GOLDEN / "engine.txt"
    if os.environ.get("HALO_SOCIAL_UPDATE") == "1" and not social.big_endian():
        path.write_text(output)
    assert output == path.read_text(), "the engine's messages changed (or differ on this byte order)"
    lines = output.splitlines()
    assert len(lines) > 20
    # the conversation holds every kind: requests, presence, direct and party messages
    kinds = {bytes.fromhex(line.split()[3])[0:1] for line in lines}
    assert kinds >= {b"R", b"P", b"D", b"G"}


FAULTS = [
    # presence never repeated: friends go offline after 150 seconds
    ("sim", "social_core.c", "	else if (!social_elapsed(engine, engine->presence_time, SOCIAL_HEARTBEAT) || !presence_publish(engine, &presence, 0, 0))", "	else if (1)"),
    # a roster longer than eight read into a list of eight
    ("fuzz-pipeline", "social_party.c", "		if (state->roster_count > SOCIAL_MAX_PARTY_MEMBERS)\n			return 0;", "		(void)state->roster_count;"),
    # a text not cleaned where it is read (a name with a pipe or a control character)
    ("fuzz-pipeline", "social_codec.c", "		text_safe_text(text, (short)size, raw, (short)length, (short)maximum);", "		memcpy(text, raw, length), text[length] = 0;"),
    # a field written in the machine's order, not big-endian
    ("golden-engine", "social_codec.c", "void social_put16(struct social_writer *writer, unsigned int value)\n{\n	social_put8(writer, value >> 8);\n	social_put8(writer, value);\n}", "void social_put16(struct social_writer *writer, unsigned int value)\n{\n	social_put8(writer, value);\n	social_put8(writer, value >> 8);\n}"),
    # the party's STATE signed without its token
    ("golden-engine", "social_party.c", "	social_sign(&engine->identity, \"hceu-f/party\", engine->party.token, SOCIAL_TOKEN_SIZE, writer->bytes, writer->size, signature);", "	social_sign(&engine->identity, \"hceu-f/party\", engine->party.token, 0, writer->bytes, writer->size, signature);"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_hard_check", mutations=((source, before, after),))
    arguments = {"sim": ["sim", "12"], "fuzz-pipeline": ["fuzz-pipeline", "20000"], "golden-engine": ["golden-engine"]}[case]
    status, output, errors = social.run(executable, *arguments)
    if case == "golden-engine" and status == 0:
        assert output != (GOLDEN / "engine.txt").read_text(), f"the fault changed no byte of the conversation ({case})"
    else:
        assert status != 0, f"the fault was not caught by {case}"
