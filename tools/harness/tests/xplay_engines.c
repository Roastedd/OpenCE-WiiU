/* xplay_engines.c: one game type's state the host sends its clients in the game state message (game_engine_*_write_network_state:
capture the flag, oddball, king of the hill, slayer, race, and infection), built once for each (their sources define alike names, so
they cannot be in one program): the state structure filled with a value of its own in every scalar, put through the game's own function
that turns it to the wire's order (little-endian) and back, and expected little-endian at each scalar's offset. Prints a line for the
golden file (tests/xplay_golden/engines.txt) both byte orders must equal.

Defined by the build: XPLAY_ENGINE_SOURCE (the game type's file), XPLAY_ENGINE_INC (its layout table), XPLAY_STRUCTURES (that table),
XPLAY_STATE (the structure's name in it), XPLAY_STATE_TYPE (its type) and XPLAY_WIRE_ORDER (the function that turns it). */

#include "xplay_common.h"
#include XPLAY_ENGINE_SOURCE
#include XPLAY_ENGINE_INC

#define COUNTOF(array) ((long)(sizeof(array) / sizeof((array)[0])))

static unsigned char made[8192], expected[8192], work[8192];

int main(int argc, char **argv)
{
	const struct xplay_structure *structure = NULL;
	long index;

	(void)argc;
	(void)argv;
	for (index = 0; index < COUNTOF(XPLAY_STRUCTURES); index++)
		if (!strcmp(XPLAY_STRUCTURES[index].name, XPLAY_STATE))
			structure = &XPLAY_STRUCTURES[index];
	XPLAY_CHECK(structure != NULL && structure->size <= (long)sizeof(made), "no layout for %s", XPLAY_STATE);
	if (!structure)
		return XPLAY_FAIL_STATUS;
	xplay_fill(structure, made, expected, 5);
	memcpy(work, made, (size_t)structure->size);
	XPLAY_WIRE_ORDER((XPLAY_STATE_TYPE *)work);
	for (index = 0; index < structure->size && work[index] == expected[index]; index++)
		;
	XPLAY_CHECK(index == structure->size, "%s: byte +%ld on the wire is %02x, not %02x", XPLAY_STATE, index, work[index],
		expected[index]);
	xplay_hex(XPLAY_STATE, work, structure->size);
	XPLAY_WIRE_ORDER((XPLAY_STATE_TYPE *)work);
	XPLAY_CHECK(!memcmp(work, made, (size_t)structure->size), "%s: received, not as sent", XPLAY_STATE);
	return xplay_failures ? XPLAY_FAIL_STATUS : 0;
}
