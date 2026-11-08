/*
BOTS_PROFILE.C (test)

A bot's own skill (bots_profile.c, compiled as it is): at the default every bot plays at bots.skill, read each tick, as
the one setting made all of them; a level of its own sticks, and is its own. test_bots_profile.py takes the code, its
header and the levels' enum from the sources.
*/

#include "harness.h"

#include "bots_combat.h"
#include "bots_profile.h"
#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct bots_profile a, b;
	short setting, tick;

	/* a new bot plays at the setting's level, follows it, and is neutral */
	CASE("new")
	{
		bots_profile_reset(&a, _bots_skill_heroic);
		CHECK(a.skill == _bots_skill_heroic && a.skill_override == BOTS_PROFILE_FOLLOWS_SETTING, "skill %d override %d",
			a.skill, a.skill_override);
		CHECK(a.aggression == 0.5f && a.power_hunger == 0.5f && a.grenade_happiness == 0.5f && a.camping == 0.5f &&
			a.spread == 0.0f, "not neutral");
		/* (an out-of-range setting is normal, as bots_skill's unknown names are) */
		bots_profile_reset(&a, 99);
		CHECK(a.skill == _bots_skill_normal, "a setting of 99 is level %d", a.skill);
		bots_profile_reset(&a, -3);
		CHECK(a.skill == _bots_skill_normal, "a setting of -3 is level %d", a.skill);
		return 0;
	}
	/* the default: the bot is at the setting's level every tick, whichever it is and however it changes (what the single
	global did for every bot) */
	CASE("follows-the-setting")
	{
		bots_profile_reset(&a, _bots_skill_normal);
		bots_profile_reset(&b, _bots_skill_normal);
		for (tick = 0; tick < 24; tick++)
		{
			setting = (short)((tick / 2) % NUMBER_OF_BOTS_SKILLS);
			CHECK(bots_profile_resolve(&a, setting) == setting && a.skill == setting, "tick %d: level %d, the setting %d", tick,
				a.skill, setting);
			CHECK(bots_profile_resolve(&b, setting) == setting, "the second bot is not at the setting");
		}
		return 0;
	}
	/* a level of its own sticks whatever the setting does, and is the bot's alone */
	CASE("own-level")
	{
		bots_profile_reset(&a, _bots_skill_normal);
		bots_profile_reset(&b, _bots_skill_normal);
		CHECK(bots_profile_set_skill(&a, _bots_skill_legendary), "a valid level refused");
		CHECK(a.skill == _bots_skill_legendary, "not at once");
		for (setting = 0; setting < NUMBER_OF_BOTS_SKILLS; setting++)
		{
			CHECK(bots_profile_resolve(&a, setting) == _bots_skill_legendary, "the setting %d moved a bot with a level of its own",
				setting);
			CHECK(bots_profile_resolve(&b, setting) == setting, "the other bot did not follow the setting %d", setting);
		}
		/* back to the setting */
		CHECK(bots_profile_set_skill(&a, BOTS_PROFILE_FOLLOWS_SETTING), "following refused");
		CHECK(bots_profile_resolve(&a, _bots_skill_easy) == _bots_skill_easy, "it does not follow again");
		return 0;
	}
	/* a level out of range is refused and changes nothing */
	CASE("refused")
	{
		bots_profile_reset(&a, _bots_skill_normal);
		bots_profile_set_skill(&a, _bots_skill_easy);
		CHECK(!bots_profile_set_skill(&a, NUMBER_OF_BOTS_SKILLS), "a level past the last accepted");
		CHECK(!bots_profile_set_skill(&a, -2), "a level below the first accepted");
		CHECK(a.skill_override == _bots_skill_easy && a.skill == _bots_skill_easy, "a refused level changed the bot");
		return 0;
	}
	/* the temperament and the spread held to their ranges */
	CASE("clamp")
	{
		bots_profile_reset(&a, _bots_skill_normal);
		a.aggression = 3.0f;
		a.power_hunger = -1.0f;
		a.grenade_happiness = 0.25f;
		a.camping = 2.0f;
		a.spread = 0.5f;
		bots_profile_clamp(&a);
		CHECK(a.aggression == 1.0f && a.power_hunger == 0.0f && a.grenade_happiness == 0.25f && a.camping == 1.0f,
			"temperament %.2f %.2f %.2f %.2f", a.aggression, a.power_hunger, a.grenade_happiness, a.camping);
		CHECK(a.spread == BOTS_PROFILE_MAXIMUM_SPREAD, "spread %.3f", a.spread);
		a.spread = -0.1f;
		bots_profile_clamp(&a);
		CHECK(a.spread == 0.0f, "negative spread %.3f", a.spread);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
