"""Parties over the fake brokers (port/linux/game/social_party.c; tests/social_party_check.c): making one, inviting, joining by an invite or a friend's open party, eight
at most, a leader that leaves, hands over, falls silent, two claims of one term and a partition that heals, kicking, party chat and its limits, following the leader's game
(full, no map, another game, opting out) and a malicious member or outsider. The negative controls break a source and show a named case fail."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

CASES = ["basic", "eight", "leader-dies", "leave-transfer", "silent-member", "kick", "chat", "open-party", "partition", "follow", "abuse"]


@pytest.fixture(scope="module")
def driver():
    return social.build("social_party_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


FAULTS = [
    # anyone is listed
    ("abuse", "social_party.c", "			admit = invited_key(engine, state.key) || (party->open && social_friend_by_key(engine, state.key)) || dropped_key(engine, state.key);", "			admit = 1;"),
    # a friend is let in when the party is closed
    ("open-party", "social_party.c", "(party->open && social_friend_by_key(engine, state.key))", "(social_friend_by_key(engine, state.key) != NULL)"),
    # a silent leader is never replaced
    ("leader-dies", "social_party.c", "		if (party->leader < 0 || social_elapsed(engine, party->leader_heard, SOCIAL_PARTY_LEADER_SILENCE))", "		if (party->leader < 0)"),
    # signatures not checked
    ("abuse", "social_party.c", "	engine->statistics.verifications++;\n	if (!social_verify(\"hceu-f/party\"", "	engine->statistics.verifications++;\n	if (0 && !social_verify(\"hceu-f/party\""),
    # a member's window not looked at
    ("abuse", "social_party.c", "	if (known && !social_window_check(&party->members[index].window, inner->generation, inner->sequence))", "	if (0)"),
    # a kicked member may come back
    ("kick", "social_party.c", "			if (party->banned_count < SOCIAL_MAX_PARTY_MEMBERS)\n				memcpy(party->banned[party->banned_count++], member->hash, SOCIAL_BLOCK_SIZE);", "			(void)party->banned_count;"),
    # two claims of one term: the lower place not preferred
    ("partition", "social_party.c", "		if (state.roster_index > current || (state.roster_index == current && memcmp(state.key, party->members[party->leader].key, SOCIAL_KEY_SIZE) > 0))\n			return;", "		if (0)\n			return;"),
    # members passed over are not remembered
    ("partition", "social_party.c", "static void remember_dropped(struct social_engine *engine, const struct social_member *member)\n{\n	struct social_party *party = &engine->party;\n	int index, slot = 0;\n", "static void remember_dropped(struct social_engine *engine, const struct social_member *member)\n{\n	struct social_party *party = &engine->party;\n	int index, slot = 0;\n\n	if (member)\n		return;\n"),
    # a member's chat is not rate limited
    ("chat", "social_party.c", "	if (++member->chat_count > SOCIAL_PARTY_CHAT_RATE)", "	if (++member->chat_count < 0)"),
    # a sender's chat is not rate limited
    ("chat", "social_party.c", "	if (recent >= 3 || (party->chat_sent_count", "	if (recent >= 3000 || (party->chat_sent_count"),
    # the full game is not tried again
    ("follow", "social_party.c", "		if (!party->follow_final_retry && party->follow_tries + 1 < SOCIAL_PARTY_FOLLOW_TRIES)", "		if (!party->follow_final_retry && 0)"),
    # a machine in a game is moved out of it
    ("follow", "social_party.c", "	if (engine->place.activity != SOCIAL_MENUS && engine->place.activity != SOCIAL_BUSY && engine->place.activity != SOCIAL_LOBBY)\n		return 0;", "	(void)engine->place.activity;"),
    # the leader invites a ninth
    ("eight", "social_party.c", "	if (member_count(party) >= SOCIAL_MAX_PARTY_MEMBERS)\n		return SOCIAL_PARTY_FULL;", "	if (member_count(party) > 20)\n		return SOCIAL_PARTY_FULL;"),
    # any outsider may claim the lead
    ("abuse", "social_party.c", "	if (!known)\n	{\n		int admit = 0;", "	if (!known)\n	{\n		int admit = 1;"),
    # the leader's list not followed: members stay when not listed
    ("silent-member", "social_party.c", "		if (other->used && index != party->self && index != party->leader && roster_position(party, other->hash) < 0)\n		{", "		if (0)\n		{"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_party_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
