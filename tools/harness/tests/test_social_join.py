"""Invites and joining a friend's game (port/linux/game/social_invite.c, social_join.c; tests/social_join_check.c): an invite to the hosted game and what its receiver may
hold, a friend's presence with the game it can be joined at, and the join machine against a stand-in for internet play, with each failure as a word and a sentence. The
negative controls break a source and show a named case fail."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

CASES = ["invite", "invite-abuse", "joinable-presence", "join-ok", "join-failures", "join-checks", "join-left", "join-through-the-network"]


@pytest.fixture(scope="module")
def driver():
    return social.build("social_join_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


FAULTS = [
    # no pause between invites to one friend
    ("invite", "social_invite.c", "	else if (friend->invite_valid && !social_elapsed(engine, friend->invite_time, SOCIAL_INVITE_SPACING))", "	else if (0)"),
    # no cap on the invites one friend holds
    ("invite", "social_invite.c", "	if (per_friend >= SOCIAL_MAX_INVITES)", "	if (per_friend < 0)"),
    # invites never lapse
    ("invite", "social_invite.c", "social_elapsed(engine, engine->invites[index].received, SOCIAL_INVITE_LIFE)", "0"),
    # an invite that is not marked in the window can be replayed
    ("invite-abuse", "social_invite.c", "	social_window_mark(&friend->window, inner->generation, inner->sequence);\n	engine->statistics.accepted++;\n	/* the same game again", "	engine->statistics.accepted++;\n	/* the same game again"),
    # a game with a password or not open shown as joinable
    ("joinable-presence", "social_core.c", "	int joinable = place->joinable && engine->details != SOCIAL_DETAILS_NONE;", "	int joinable = place->has_invite && engine->details != SOCIAL_DETAILS_NONE;"),
    # the version is not looked at before trying
    ("join-checks", "social_join.c", "	if (target->network_version != backend->version(backend->user))", "	if (0)"),
    # the map is not looked at
    ("join-checks", "social_join.c", "	if (map_state == 2)", "	if (0)"),
    # a machine in a game is moved to another
    ("join-checks", "social_join.c", "	if (backend->busy(backend->user))", "	if (0)"),
    # a game full is called closed
    ("join-failures", "social_join.c", "	if (!strcmp(word, \"full\") || !strcmp(word, \"address_full\"))\n		return SOCIAL_JOIN_REASON_FULL;", "	if (!strcmp(word, \"address_full\"))\n		return SOCIAL_JOIN_REASON_FULL;"),
    # the strict NAT not told
    ("join-failures", "social_join.c", "		return machine->backend.strict_nat && machine->backend.strict_nat(machine->backend.user) ? SOCIAL_JOIN_REASON_STRICT_NAT : SOCIAL_JOIN_REASON_NO_ROUTE;", "		return SOCIAL_JOIN_REASON_NO_ROUTE;"),
    # a friend who has left is not noticed
    ("join-left", "social_join.c", "	if (!machine->target.invited && machine->target.friend_id && (unsigned int)(now(machine) - machine->checked) >= JOIN_CHECK_INTERVAL)", "	if (0)"),
    # no timeout
    ("join-left", "social_join.c", "	if ((unsigned int)(now(machine) - machine->started) >= JOIN_TIMEOUT)", "	if (0)"),
    # a new invite tried again and again
    ("join-left", "social_join.c", "		if (shown && !machine->retried)", "		if (shown)"),
    # a cancelled join not told to internet play
    ("join-left", "social_join.c", "	if (machine->state == SOCIAL_JOIN_WORKING)\n		machine->backend.cancel(machine->backend.user);\n	if (machine->state != SOCIAL_JOIN_IDLE)", "	if (machine->state != SOCIAL_JOIN_IDLE)"),
    # the game's readiness not waited for
    ("join-ok", "social_join.c", "		if (!backend->game_ready || backend->game_ready(backend->user, machine->target.host_hash))", "		if (1)"),
    # the host's game of the friend not looked at in the network case
    ("join-through-the-network", "social_join.c", "		if (shown && !memcmp(again.token, machine->target.token, SOCIAL_TOKEN_SIZE))\n			return;", "		if (1)\n			return;"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_join_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
