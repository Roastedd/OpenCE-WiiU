"""The friends layer as the social screens' backend (port/linux/game/screens_social_backend.c; tests/screens_social_check.c) on the fake brokers: a friend added by its typed code,
seen in the FRIENDS tab's model, invited, and the invitation a toast on the other machine; joining by a row or an invitation and its refusals in plain words; the requests,
blocked and party tabs and the actions on their rows; the roster's friend and party flags; the hold of the "online" toasts; and the screens with the layer off. Each negative control
breaks the backend's source (a copy of it) and shows a named case fail."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

CASES = ["befriend-and-invite", "join-words", "invite-in-game", "actions", "party", "roster-flags", "online-toasts", "off", "generation", "clean-text"]
FILE = "screens_social_backend.c"


@pytest.fixture(scope="module")
def driver():
    return social.build("screens_social_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


FAULTS = [
    # this machine's own code is sent to the layer as if it were a friend's (the layer answers it later, and the screen has said "sent")
    ("befriend-and-invite", FILE, "	if (be_code_same(text, be.snapshot.code))", "	if (0)"),
    # a friend already made is asked again
    ("befriend-and-invite", FILE, "		if (be_code_same(text, be.snapshot.friends[index].code))", "		if (0)"),
    # a refusal of the layer is said as if it were sent
    ("befriend-and-invite", FILE, "	if (result != SOCIAL_ADD_RESULT_SENT)\n	{\n		be_answer_sentence(answer, size, social_add_result_text(result));\n		return 0;\n	}",
     "	(void)result;"),
    # the service is always connected
    ("off", FILE, "	if (!be.have)\n		return SCREENS_SERVICE_DISABLED;", "	if (0)\n		return SCREENS_SERVICE_DISABLED;"),
    # the generation never moves
    ("generation", FILE, "	return (unsigned long)be.version * 1000003ul + (unsigned long)be.changes * 7919ul + (unsigned long)status.serial * 31ul + (unsigned long)status.state;",
     "	return 1;"),
    # an action of ours does not move the generation
    ("generation", FILE, "	return (unsigned long)be.version * 1000003ul + (unsigned long)be.changes * 7919ul +", "	return (unsigned long)be.version * 1000003ul +"),
    # a private game shown as closed
    ("join-words", FILE, "		if (private_game)\n			return SCREENS_JOIN_INVITE_ONLY;", "		if (0)\n			return SCREENS_JOIN_INVITE_ONLY;"),
    # the friend's game is never joinable
    ("join-words", FILE, "	return SCREENS_JOIN_OPEN;\n}\n\nstatic void be_friend_row", "	return SCREENS_JOIN_CLOSED;\n}\n\nstatic void be_friend_row"),
    # a missing map is not told
    ("join-words", FILE, "		if (state == 2)\n			return SCREENS_JOIN_NEEDS_MAP;", "		if (state == 99)\n			return SCREENS_JOIN_NEEDS_MAP;"),
    # a join's refusal is not given in words
    ("join-words", FILE, "		if (reason != SOCIAL_JOIN_REASON_NONE)\n		{\n			be_answer_sentence(answer, size, social_join_reason_text(reason, invite ? invite->target.map : info->map));\n			return 0;\n		}",
     "		(void)reason;"),
    # an invitation left on the friend's row is not used to join
    ("join-words", FILE, "		int reason = invite ? social_join_invite(invite->id) : social_join_friend(id);", "		int reason = social_join_friend(id);"),
    # a join's failure is told every time
    ("join-words", FILE, "		be.join_state_told = 1;\n		be_sentence(text, sizeof(text), status.text[0]", "		be_sentence(text, sizeof(text), status.text[0]"),
    # an invitation answered in a game is joined over it
    ("invite-in-game", FILE, "	if (social_join_hook_busy())\n	{", "	if (0)\n	{"),
    # the invitation is dropped when left for later
    ("invite-in-game", FILE, "	if (!accept)\n		return;", "	if (!accept)\n	{\n		social_invite_dismiss(invite->id);\n		return;\n	}"),
    # a request's BLOCK blocks the friend whose id it shares
    ("actions", FILE, "		if (row->presence == SCREENS_PRESENCE_REQUEST_GOT)\n			social_ignore(id);\n		else if (be_friend_by_id(id)", "		if (0)\n			social_ignore(id);\n		else if ((1 || be_friend_by_id(id))"),
    # an action on a row of another kind is done
    ("actions", FILE, "		if (row->presence != SCREENS_PRESENCE_REQUEST_GOT)\n			break;\n		social_accept(id);", "		social_accept(id);"),
    # the party's disband is offered, and sent as a leave
    ("party", FILE, "		if (all[index] == SCREENS_ACTION_DISBAND_PARTY)\n			actions[count++] = SCREENS_ACTION_LEAVE_PARTY;\n		else\n			actions[count++] = all[index];",
     "		actions[count++] = all[index];"),
    # the party invitation does not make the party
    ("party", FILE, "		if (!be.snapshot.party.active)\n			social_party_make();\n		social_party_invite(id);", "		social_party_invite(id);"),
    # the friend by name in any game
    ("roster-flags", FILE, "	if (!a || !a[0] || !b || !b[0])\n		return 1;", "	if (1)\n		return 1;"),
    # a friend in the menus flagged as in the game
    ("roster-flags", FILE, "		if (!info->online || (info->activity != SOCIAL_ACTIVITY_LOBBY && info->activity != SOCIAL_ACTIVITY_GAME))\n			continue;", "		if (!info->online)\n			continue;"),
    # the online toast is told at once
    ("online-toasts", FILE, "	ONLINE_HOLD_MILLISECONDS = SCREENS_TOAST_ONLINE_HOLD_MILLISECONDS,", "	ONLINE_HOLD_MILLISECONDS = 0,"),
    # a layer that is off is read as on
    ("off", FILE, "	be.have = be.version != 0 && be.snapshot.enabled;", "	be.have = 1;"),
    # a toast's text not capitalised
    ("befriend-and-invite", FILE, "	screens_social_upper(who, sizeof(who), name && name[0] ? name : \"SOMEONE\");", "	snprintf(who, sizeof(who), \"%s\", name && name[0] ? name : \"SOMEONE\");"),
    # an invite's game not in the detail
    ("befriend-and-invite", FILE, "		snprintf(detail, (size_t)size, \"%s - %s - %d of %d\"", "		snprintf(detail, (size_t)size, \"%s %s %d of %d\""),
    # roster: a stranger's code asked for friendship
    ("clean-text", FILE, "		be_answer_set(answer, size, \"THAT PLAYER'S CODE IS NOT KNOWN. ASK FOR IT: FRIENDS > ADD FRIEND.\");\n		return 0;", "		return 1;"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("screens_social_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
