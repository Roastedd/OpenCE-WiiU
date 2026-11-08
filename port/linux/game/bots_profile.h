/* bots_profile.h: a bot's own skill and temperament (port/linux/game/bots_profile.c)

The bots' skill was one setting for all of them (bots.skill, read each tick). A bot now has a profile of its own: the
level it plays at, and the temperament that will tell bots of one level apart. The level follows the setting unless the
bot has been given one of its own (a Players page's SKILL, the debug.bots_skills of the tests), so at the default, every
bot plays at bots.skill as the one setting made them, the tick it is changed too.

Plain data and rules with no call into the engine, so that the harness runs them as they are
(tools/harness/tests/test_bots_profile.py). bots.c reads the setting and the table of the levels. */
#ifndef HALO_BOTS_PROFILE_H
#define HALO_BOTS_PROFILE_H

/* skill_override: the level follows bots.skill */
#define BOTS_PROFILE_FOLLOWS_SETTING (-1)

/* the most a bot's spread may be (the multiplicative jitter on the numbers of its level) */
#define BOTS_PROFILE_MAXIMUM_SPREAD 0.15f

struct bots_profile
{
	/* the level it plays at now (bots_combat.h's order) */
	short skill;
	/* its own level, or BOTS_PROFILE_FOLLOWS_SETTING */
	short skill_override;
	/* 0 to 1, neutral at 0.5: how aggressive it is, how much it wants the power weapons, how happy it is to throw
	grenades, how much it camps. Not read yet: the behaviour that uses them comes with the items and the objectives. */
	float aggression;
	float power_hunger;
	float grenade_happiness;
	float camping;
	/* 0 to BOTS_PROFILE_MAXIMUM_SPREAD, 0 for none: the spread of the numbers of its level. Not read yet. */
	float spread;
};

/* a new bot: playing at the setting's level, following it, neutral */
void bots_profile_reset(struct bots_profile *profile, short setting_skill);
/* a level in range, else NORMAL (as an unknown bots.skill is) */
short bots_profile_valid_skill(short skill);
/* each tick: the bot's level, which is its own if it has one, else the setting's (read once a tick for all the bots) */
short bots_profile_resolve(struct bots_profile *profile, short setting_skill);
/* the bot's own level (an out-of-range one is refused: FALSE, nothing changed), or BOTS_PROFILE_FOLLOWS_SETTING to follow the
setting again */
int bots_profile_set_skill(struct bots_profile *profile, short skill);
/* the numbers held to their ranges (a temperament outside 0 to 1, a spread outside its range) */
void bots_profile_clamp(struct bots_profile *profile);

#endif
