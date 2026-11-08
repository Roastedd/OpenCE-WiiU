#include "harness.h"

/* The wall clock of internet play (port/linux/src/halo_clock.h, anchored by a TLS answer's Date; p2p.c's halo_utc_now) and the server browser's use of it
(port/linux/src/p2p_lobby.c, listing_current): a console whose own clock is hours off still sees the listings of machines whose clocks are right, and signs
its own so that they do. */
#include "config.inc"
#include "halo_clock.h"

/* the machine under test: its own clock (set at will, as a user does) and the monotonic one */
static double wall;
static unsigned int mono;
static struct halo_clock clock_state;

static unsigned long long halo_utc_now(void)
{
	return (unsigned long long)halo_clock_now(&clock_state, mono, wall);
}

#include "under_test.inc"

/* a listing of the time given, as a host of that clock makes it */
static struct listing listing_at(double when)
{
	struct listing listing;

	memset(&listing, 0, sizeof(listing));
	listing.time = (unsigned long)when;
	return listing;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	/* 2026-11-07 12:00:00 UTC */
	const double truth = 1794052800.0;

	memset(&clock_state, 0, sizeof(clock_state));
	wall = truth;
	mono = 5000;

	/* with no anchor the machine's own clock is the time, as it was */
	CASE("unanchored")
	{
		CHECK(halo_clock_now(&clock_state, mono, wall) == wall, "unanchored");
		mono += 90000;
		wall += 90.0;
		CHECK(halo_clock_now(&clock_state, mono, wall) == wall, "unanchored later");
	}

	/* a console 25,200 seconds behind (the offset measured on the console) drops every listing of a PC without the anchor, and sees it with one */
	CASE("skew-seen-without-anchor-lost")
	{
		struct listing pc = listing_at(truth);

		wall = truth - 25200.0;
		CHECK(!listing_current(&pc, 0), "a skewed console takes a listing of the right time");
		CHECK(!listing_current(&pc, 1), "a skewed console takes a retained listing of the right time");
	}
	CASE("skew-anchored-sees-listings")
	{
		struct listing pc = listing_at(truth);
		struct listing old_pc = listing_at(truth - 3000.0);

		wall = truth - 25200.0;
		CHECK(halo_clock_anchor(&clock_state, truth, mono, wall) == 1, "anchored");
		CHECK(halo_utc_now() == (unsigned long long)truth, "now");
		CHECK(listing_current(&pc, 0) && listing_current(&pc, 1), "a PC's listing is current");
		/* the windows are as they were: a retained listing of 50 minutes ago is not, a live one is */
		CHECK(!listing_current(&old_pc, 1) && listing_current(&old_pc, 0), "windows");
		/* the user changes the console's clock afterwards: the anchor holds */
		wall = truth + 99999.0;
		mono += 30000;
		CHECK(halo_utc_now() == (unsigned long long)truth + 30, "monotonic after the anchor: %llu", halo_utc_now());
	}

	/* a console 7 hours ahead signs its listing with the corrected time: a PC (right clock) takes it */
	CASE("skew-signing")
	{
		unsigned long signed_time;
		struct listing console;

		wall = truth + 25200.0;
		halo_clock_anchor(&clock_state, truth, mono, wall);
		signed_time = (unsigned long)halo_utc_now();
		console = listing_at((double)signed_time);
		CHECK(signed_time == (unsigned long)truth, "signed %lu", signed_time);
		CHECK(halo_clock_listing_difference(console.time, truth) == 0, "a PC reads it as now");
		CHECK(halo_clock_listing_difference(console.time, truth + 25200.0) < -3600, "the unanchored console's would not be");
	}

	/* a later Date within a few seconds of the estimate leaves the anchor, one further moves it */
	CASE("steady")
	{
		wall = truth - 100.0;
		CHECK(halo_clock_anchor(&clock_state, truth, mono, wall), "first");
		mono += 60000;
		CHECK(!halo_clock_anchor(&clock_state, truth + 60.0 + 2.0, mono, wall), "2 seconds off: kept");
		CHECK(!halo_clock_anchor(&clock_state, truth + 60.0 - 3.0, mono, wall), "3 seconds off: kept");
		CHECK(halo_clock_anchor(&clock_state, truth + 60.0 + 4.0, mono, wall), "4 seconds off: moved");
		CHECK(halo_clock_now(&clock_state, mono, wall) == truth + 64.0, "moved to it");
	}

	/* what cannot be a time is refused */
	CASE("refused")
	{
		CHECK(!halo_clock_anchor(&clock_state, 0.0, mono, wall), "zero");
		CHECK(!halo_clock_anchor(&clock_state, 946684799.0, mono, wall), "before 2000");
		CHECK(!halo_clock_anchor(&clock_state, 4102444801.0, mono, wall), "after 2100");
		CHECK(!halo_clock_anchor(&clock_state, -5.0, mono, wall), "negative");
		CHECK(!halo_clock_anchor(&clock_state, __builtin_nan(""), mono, wall), "not a number");
		CHECK(!clock_state.anchored, "still unanchored");
		CHECK(halo_clock_now(&clock_state, mono, wall) == wall, "the machine's clock");
	}

	/* the monotonic millisecond count wraps at 2^32 (49.7 days): across it, and over a run of 200 days read once a week */
	CASE("wrap")
	{
		unsigned int start = 0xFFFFFF00u;
		int week;

		CHECK(halo_clock_anchor(&clock_state, truth, start, wall), "anchored");
		CHECK(halo_clock_now(&clock_state, start + 0x400u, wall) == truth + 1.024, "across the wrap: %f", halo_clock_now(&clock_state, start + 0x400u, wall) - truth);
		memset(&clock_state, 0, sizeof(clock_state));
		halo_clock_anchor(&clock_state, truth, start, wall);
		for (week = 1; week <= 30; week++)
		{
			double now = halo_clock_now(&clock_state, start + (unsigned int)((unsigned long long)week * 7 * 86400 * 1000), wall);

			CHECK(now == truth + week * 7.0 * 86400.0, "week %d: %f", week, now - truth);
		}
	}

	/* the 32-bit time of a listing across the end of 2106 reads as the small difference it is */
	CASE("listing-difference")
	{
		CHECK(halo_clock_listing_difference(100, 90.0) == 10, "ahead");
		CHECK(halo_clock_listing_difference(90, 100.0) == -10, "behind");
		CHECK(halo_clock_listing_difference(5, 4294967290.0) == 11, "wrapped");
	}
	return 0;
}
