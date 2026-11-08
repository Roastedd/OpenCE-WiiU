"""A bot's own skill (port/linux/game/bots_profile.c, compiled as it is): at the default each bot plays at bots.skill, read
each tick, as the one setting made all of them; a level of its own sticks and is its own; a level out of range is refused."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["new", "follows-the-setting", "own-level", "refused", "clamp"]

# a fault in bots_profile.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "own-level-ignored": (("profile->skill_override == BOTS_PROFILE_FOLLOWS_SETTING ?", "1 ?"), "own-level"),
    "setting-ignored": (("bots_profile_valid_skill(setting_skill) : bots_profile_valid_skill(profile->skill_override)",
                         "bots_profile_valid_skill(profile->skill) : bots_profile_valid_skill(profile->skill_override)"),
                        "follows-the-setting"),
    "out-of-range-accepted": (("if (skill < 0 || skill >= NUMBER_OF_BOTS_SKILLS)\n\t\treturn 0;\n", ""), "refused"),
    "unknown-level-not-normal": (("skill < NUMBER_OF_BOTS_SKILLS ? skill : _bots_skill_normal;",
                                  "skill < NUMBER_OF_BOTS_SKILLS ? skill : _bots_skill_easy;"), "new"),
    "following-not-restored": (("\tif (skill == BOTS_PROFILE_FOLLOWS_SETTING)\n\t{\n\t\tprofile->skill_override = BOTS_PROFILE_FOLLOWS_SETTING;\n",
                                "\tif (skill == BOTS_PROFILE_FOLLOWS_SETTING)\n\t{\n"), "own-level"),
    "spread-unbounded": (("BOTS_PROFILE_MAXIMUM_SPREAD);", "1.0f);"), "clamp"),
    "temperament-unbounded": (("profile->camping = bots_profile_clamp_float(profile->camping, 0.0f, 1.0f);", ""), "clamp"),
    "not-neutral": (("profile->camping = 0.5f;", "profile->camping = 0.0f;"), "new"),
}


def generated(fault=None):
    code = read("port/linux/game/bots_profile.c")
    if fault:
        code = mutated(code, *fault)
    return (("bots_combat.h", read("port/linux/game/bots_combat.h")), ("bots_profile.h", read("port/linux/game/bots_profile.h")),
            ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_profile", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_profile", generated(fault)), case)
    assert status == CHECK_FAILED, f"the profile with a fault ({control}) passed '{case}': the test cannot see it"
