/*
BOTS_PROFILE.C

A bot's own skill and temperament (bots_profile.h). Plain rules, no engine: the harness compiles this file as it is.
*/

#include "bots_combat.h"
#include "bots_profile.h"

static float bots_profile_clamp_float(float value, float low, float high)
{
	return value < low ? low : value > high ? high : value;
}

short bots_profile_valid_skill(short skill)
{
	return skill >= 0 && skill < NUMBER_OF_BOTS_SKILLS ? skill : _bots_skill_normal;
}

void bots_profile_reset(struct bots_profile *profile, short setting_skill)
{
	profile->skill = bots_profile_valid_skill(setting_skill);
	profile->skill_override = BOTS_PROFILE_FOLLOWS_SETTING;
	profile->aggression = 0.5f;
	profile->power_hunger = 0.5f;
	profile->grenade_happiness = 0.5f;
	profile->camping = 0.5f;
	profile->spread = 0.0f;
}

short bots_profile_resolve(struct bots_profile *profile, short setting_skill)
{
	profile->skill = profile->skill_override == BOTS_PROFILE_FOLLOWS_SETTING ?
		bots_profile_valid_skill(setting_skill) : bots_profile_valid_skill(profile->skill_override);
	return profile->skill;
}

int bots_profile_set_skill(struct bots_profile *profile, short skill)
{
	if (skill == BOTS_PROFILE_FOLLOWS_SETTING)
	{
		profile->skill_override = BOTS_PROFILE_FOLLOWS_SETTING;
		return 1;
	}
	if (skill < 0 || skill >= NUMBER_OF_BOTS_SKILLS)
		return 0;
	profile->skill_override = skill;
	profile->skill = skill;
	return 1;
}

void bots_profile_clamp(struct bots_profile *profile)
{
	profile->aggression = bots_profile_clamp_float(profile->aggression, 0.0f, 1.0f);
	profile->power_hunger = bots_profile_clamp_float(profile->power_hunger, 0.0f, 1.0f);
	profile->grenade_happiness = bots_profile_clamp_float(profile->grenade_happiness, 0.0f, 1.0f);
	profile->camping = bots_profile_clamp_float(profile->camping, 0.0f, 1.0f);
	profile->spread = bots_profile_clamp_float(profile->spread, 0.0f, BOTS_PROFILE_MAXIMUM_SPREAD);
}
