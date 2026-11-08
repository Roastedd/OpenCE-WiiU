/*
HALO_CLOCK.H

The wall clock internet play compares listings with (halo_utc_now, p2p.c), as plain code that takes its clocks as arguments, so that the harness can test it
alone. A machine's own clock is not to be trusted: the Wii U's is the console's, often hours off (a time zone set as if it were UTC), and a listing is current
only within minutes of this machine's time (p2p_lobby.c). The time of a TLS answer's Date header is vouched for by a certificate, which the console's clock
cannot spoil, so a Date (http_date.h) anchors this clock: from then on it is that time plus the monotonic milliseconds since, which a change of the machine's
own clock cannot move. Until an anchor comes the machine's own clock is used, as before.
*/

#ifndef HALO_CLOCK_H
#define HALO_CLOCK_H

struct halo_clock
{
	/* whether an anchor has been taken */
	int anchored;
	/* Unix seconds in UTC at the monotonic millisecond `mono` (the clock wraps at 2^32 milliseconds, 49.7 days: the anchor is moved forward whenever the clock is
	read, so that a machine on for longer keeps it) */
	double utc;
	unsigned int mono;
	/* the machine's own clock less this clock's, in seconds, when the anchor was last taken (for the log) */
	double skew;
};

/* A sample no more than this far from the clock's own estimate leaves the anchor as it is: a Date has whole seconds and the answer took some time, so successive
samples differ by a second or two, and a clock that steps back and forth by that would make a listing's time wander for no reason */
#define HALO_CLOCK_STEADY_SECONDS 3.0

/* Unix seconds in UTC now: the anchor's, moved on by the monotonic clock; wall_seconds (the machine's own clock) when there is no anchor. mono_ms is the monotonic
clock now. */
static inline double halo_clock_now(struct halo_clock *clock, unsigned int mono_ms, double wall_seconds)
{
	unsigned int elapsed;

	if (!clock->anchored)
		return wall_seconds;
	elapsed = mono_ms - clock->mono;
	/* (moved forward by the whole seconds, the remainder kept: the anchor stays within 2^32 milliseconds of the clock) */
	if (elapsed >= 1000u)
	{
		clock->utc += (double)(elapsed / 1000u);
		clock->mono += elapsed / 1000u * 1000u;
		elapsed = mono_ms - clock->mono;
	}
	return clock->utc + (double)elapsed / 1000.0;
}

/* Takes a sample (Unix seconds in UTC, as http_date_parse gives them, less any time the answer took) at the monotonic millisecond mono_ms. 0 for a time that cannot be
one (before 2000 or after 2100) or when the clock keeps its anchor because the sample is within HALO_CLOCK_STEADY_SECONDS of it; 1 when the sample was taken as the
anchor. */
static inline int halo_clock_anchor(struct halo_clock *clock, double utc_seconds, unsigned int mono_ms, double wall_seconds)
{
	if (!(utc_seconds >= 946684800.0 && utc_seconds <= 4102444800.0))
		return 0;
	if (clock->anchored)
	{
		double estimate = halo_clock_now(clock, mono_ms, wall_seconds);

		if (utc_seconds - estimate <= HALO_CLOCK_STEADY_SECONDS && estimate - utc_seconds <= HALO_CLOCK_STEADY_SECONDS)
			return 0;
	}
	clock->anchored = 1;
	clock->utc = utc_seconds;
	clock->mono = mono_ms;
	clock->skew = wall_seconds - utc_seconds;
	return 1;
}

/* A listing's time (32 bits, Unix seconds) against now: the seconds it is ahead of now (negative: behind), as p2p_lobby.c's window reads it. Whole seconds, so that
the wrap of the 32-bit time in 2106 reads as the small difference it is. */
static inline long halo_clock_listing_difference(unsigned long listing_time, double now_seconds)
{
	return (long)(int)((unsigned int)listing_time - (unsigned int)(unsigned long long)now_seconds);
}

#endif
