/* coreinit/time.h as the game digest's input layer has it (xinput_wiiu.c's skip hold and wiiu_input_milliseconds): the time base is the digest's (game_digest_glue.c's OSGetSystemTime), at the console's rate */
#ifndef GAME_DIGEST_COREINIT_TIME_H
#define GAME_DIGEST_COREINIT_TIME_H

#include <stdint.h>

typedef int64_t OSTime;

OSTime OSGetSystemTime(void);

#define OSTimerClockSpeed 62156250
#define OSTicksToMilliseconds(ticks) ((ticks) / (OSTimerClockSpeed / 1000))
#define OSMillisecondsToTicks(ms) ((OSTime)(ms) * (OSTimerClockSpeed / 1000))
#define OSSecondsToTicks(s) ((OSTime)(s) * OSTimerClockSpeed)

#endif
