#include "clock.h"

#include <coreinit/time.h>

uint64_t clock_now_ns(void)
{
	return OSTicksToNanoseconds(OSGetSystemTime());
}
