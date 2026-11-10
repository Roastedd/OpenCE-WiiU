"""Finishing a join in the menus and leaving a game for an invitation (port/linux/game/social_join.c's flow; tests/social_flow_check.c) on the fake brokers, with stand-ins for the game: an
invitation answered in a game leaves it, waits for the menus, joins, has the Direct Link list opened over a screen that allows it, presses A at most three times and is done once this
machine is in the game; a game that cannot be joined is not worth leaving for; a game that will not go is given up in words; a screen in the way is waited on; the party's follower in
the menus joins by itself. Each negative control breaks a source and shows a named case fail."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

CASES = ["leave-and-join", "invite-checks", "leave-times-out", "ready-screens", "follow", "follow-off"]
FILE = "social_join.c"


@pytest.fixture(scope="module")
def driver():
    return social.build("social_flow_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


FAULTS = [
    # the game is not told to leave
    ("leave-and-join", FILE, "		if (flow.world.busy(flow.world.user))\n		{\n			flow.world.leave(flow.world.user);\n			return;\n		}", "		if (flow.world.busy(flow.world.user))\n			return;"),
    # the join begins without waiting for the menus
    ("leave-and-join", FILE, "		if (!flow.world.menus_up(flow.world.user))\n		{\n			flow.waiting = \"Back to the menus...\";\n			return;\n		}", "		if (0)\n			return;"),
    # the game is left for a map that is missing
    ("invite-checks", FILE, "			if (invite->target.map[0] && social_join_hook_map_state(invite->target.map) == 2)\n				return SOCIAL_JOIN_REASON_MAP_MISSING;", "			(void)0;"),
    # the game is left for the game it is in
    ("invite-checks", FILE, "			if (global_machine.backend.in_game_of(global_machine.backend.user, invite->target.host_hash))\n				return SOCIAL_JOIN_REASON_TOGETHER;", "			(void)0;"),
    # the invitation is kept after being used
    ("leave-and-join", FILE, "			social_invite_dismiss(invite_id);\n			return SOCIAL_JOIN_REASON_NONE;\n		}\n	}\n	return SOCIAL_JOIN_REASON_BAD_INVITE;\n}\n\nint social_flow_press_allowed", "			return SOCIAL_JOIN_REASON_NONE;\n		}\n	}\n	return SOCIAL_JOIN_REASON_BAD_INVITE;\n}\n\nint social_flow_press_allowed"),
    # no timeout on leaving
    ("leave-times-out", FILE, "		if ((unsigned int)(now - flow.leave_started) >= FLOW_LEAVE_TIMEOUT)", "		if (0)"),
    # the list is opened every frame
    ("leave-and-join", FILE, "			if (flow.world.menus_up(flow.world.user) && flow.opened_for != (int)status.serial)", "			if (flow.world.menus_up(flow.world.user))"),
    # a screen in the way is not waited on: the list opens over anything
    ("ready-screens", FILE, "		case SOCIAL_FLOW_SCREEN_SAFE:\n			flow.blocked_since = 0;", "		case SOCIAL_FLOW_SCREEN_OTHER:\n		case SOCIAL_FLOW_SCREEN_SAFE:\n			flow.blocked_since = 0;"),
    # a failed open counts as opened
    ("ready-screens", FILE, "				if (flow.world.open_list(flow.world.user))\n					flow.opened_for = (int)status.serial;", "				flow.world.open_list(flow.world.user);\n				flow.opened_for = (int)status.serial;"),
    # no limit on the presses
    ("ready-screens", FILE, "	if (flow.presses >= FLOW_PRESS_MAXIMUM)", "	if (flow.presses >= 1000)"),
    # a press every frame
    ("leave-and-join", FILE, "	if (flow.presses && (unsigned int)(now - flow.last_press) < FLOW_PRESS_SPACING)", "	if (0)"),
    # a join nobody finishes never lapses
    ("ready-screens", FILE, "		else if ((unsigned int)(now - flow.ready_since) >= FLOW_READY_TIMEOUT)", "		else if (0)"),
    # a join is not done once this machine is in the game
    ("leave-and-join", FILE, "			social_join_machine_done(&global_machine);\n			flow.ready_since = flow.blocked_since = 0;\n			break;", "			break;"),
    # a failed join is kept
    ("ready-screens", FILE, "		else if ((unsigned int)(now - flow.failed_since) >= FLOW_FAILED_HOLD)", "		else if (0)"),
    # the party does not follow (the join is not advanced when idle)
    ("follow", FILE, "	/* (advances the join, and starts the party's following of its leader's game when this machine is idle) */\n	social_join_update();", "	if (global_machine.state == SOCIAL_JOIN_WORKING)\n		social_join_update();"),
    # a follower in a game is moved
    ("follow", "social_party.c", "	if (engine->place.activity != SOCIAL_MENUS && engine->place.activity != SOCIAL_BUSY && engine->place.activity != SOCIAL_LOBBY)\n		return 0;", "	(void)engine->place.activity;"),
    # follow off ignored
    ("follow-off", "social_party.c", "|| !party->auto_follow ||", "||"),
    # the log says nothing of the leave
    ("leave-and-join", FILE, "		flow_log(\"flow: the menus are up; the invitation's game is joined\");\n", ""),
    # downloads off not told
    ("invite-checks", FILE, "		if (map && map[0] && hook_map_download && !hook_map_download())", "		if (0)"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_flow_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
