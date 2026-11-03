/* the pieces of the game's cseries.h that port/linux/game/tick_overlap.c and tick_owners.c use, for test_tick_overlap.c */
#ifndef __CSERIES_STUB_H
#define __CSERIES_STUB_H

#include <stdint.h>

typedef unsigned char boolean;
typedef unsigned char byte;
typedef float real;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define PIN(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

enum
{
	TICKS_PER_SECOND = 30,
	_error_silent = 0,
};

void error(int severity, char const *format, ...);
void profile_thread_disable(void);

#endif
