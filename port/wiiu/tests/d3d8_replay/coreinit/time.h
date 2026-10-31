/* coreinit/time.h as the Direct3D layer's replay on Linux has it (port/wiiu/tests/test_d3d8_replay.c): the time base, which the replay advances itself, at the
console's rate (the bus's 248.625 MHz / 4) */
#ifndef D3D8_REPLAY_COREINIT_TIME_H
#define D3D8_REPLAY_COREINIT_TIME_H

#include <stdint.h>

typedef int64_t OSTime;

OSTime OSGetSystemTime(void);

#define OSTimerClockSpeed 62156250

#endif
