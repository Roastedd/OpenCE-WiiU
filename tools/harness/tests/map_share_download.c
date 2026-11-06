/*
MAP_SHARE_DOWNLOAD.C (test)

What a map download (port/linux/game/map_share.c) does for the machine that makes it: it fails before it writes a
byte if the card has no room for the files (and says how much is needed), goes on when the room cannot be read, and
asks again while it is being read; the part file of a download on a card that cannot be written past its end (the
Wii U's) has a gap before a piece written with zeros; the Wii U fetches from the host alone over the internet; and a
game whose Custom Edition maps are off, or whose map downloads are, is told why a map was not asked for.
test_map_share_download.py takes the constants and structures (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

typedef unsigned short word;

#include "config.inc"

#define csmemset(destination, value, size) memset(destination, value, size)
#define csmemcpy(destination, source, size) memcpy(destination, source, size)
#define csmemcmp(first, second, size) memcmp(first, second, size)
#define csstrcmp(first, second) strcmp(first, second)
#define csstrncmp(first, second, count) strncmp(first, second, count)
#define csstrncpy(destination, source, count) strncpy(destination, source, count)
#define csstrlen(text) strlen(text)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define _error_silent 0
#define error(level, ...) ((void)0)
#define HALO_WIIU

/* the fake world: a clock, the settings, what was logged and what failed the download */
static unsigned long fake_now = 100000;
static int fake_map_download = 1;
static int fake_tag_cache = 1;
static int fake_host = 0;
static int fake_name_safe = 1;
static char fake_failure[512];
static int fake_failures;
static int fake_logs;

static struct map_share_globals map_share_globals;
static long volatile free_space_state;
static unsigned long long free_space_bytes;
static unsigned long free_space_asked_at;

static unsigned long system_milliseconds(void)
{
	return fake_now;
}

static void platform_log(char const *format, ...)
{
	(void)format;
	fake_logs++;
}

static void download_fail(char const *reason)
{
	snprintf(fake_failure, sizeof(fake_failure), "%s", reason);
	fake_failures++;
}

static int config_boolean(char const *name)
{
	return !strcmp(name, "network.map_download") ? fake_map_download : 0;
}

static boolean map_share_host(void)
{
	return fake_host;
}

static void *halo_custom_edition_tag_cache(void)
{
	return fake_tag_cache ? (void *)&fake_now : NULL;
}

#define custom_edition_level_name(level_name) (!strncmp(level_name, "custom_maps\\", 12))
static char const *tag_name_strip_path(char const *name)
{
	char const *slash = strrchr(name, '\\');

	return slash ? slash + 1 : name;
}

static boolean map_share_name_safe(char const *name)
{
	(void)name;
	return fake_name_safe;
}

#include "under_test.inc"

static void fresh(void)
{
	memset(&map_share_globals, 0, sizeof(map_share_globals));
	fake_now = 100000;
	fake_failures = 0;
	fake_failure[0] = 0;
	free_space_state = 0;
	free_space_bytes = 0;
	free_space_asked_at = fake_now;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct download *download = &map_share_globals.download;

	fresh();
	CASE("room-short") {
		/* a 190 MB download on a card with 200 MB free: 32 MB must stay free */
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 2;
		free_space_bytes = 200ULL << 20;
		CHECK(!download_space_check(), "the download that leaves less than the spare fails");
		CHECK(fake_failures == 1 && strstr(fake_failure, "190 MB") && strstr(fake_failure, "200 MB is free"),
			"it says how much is needed and how much is free: %s", fake_failure);
	} else CASE("room-enough") {
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 2;
		free_space_bytes = 300ULL << 20;
		CHECK(download_space_check() && !fake_failures && download->space_checked, "enough room goes on, and is checked once");
		free_space_bytes = 1;
		CHECK(download_space_check() && !fake_failures, "and is not asked again");
	} else CASE("room-exact") {
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 2;
		free_space_bytes = download->space_needed;
		CHECK(download_space_check() && !fake_failures, "the spare left exactly is enough");
		fresh();
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 2;
		free_space_bytes = download->space_needed - 1;
		CHECK(!download_space_check() && fake_failures == 1, "a byte less is not");
	} else CASE("room-unknown") {
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 3;
		CHECK(download_space_check() && !fake_failures && download->space_checked,
			"a card whose room cannot be read goes on (a write that fails still fails the download)");
	} else CASE("room-being-read") {
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 1;
		CHECK(download_space_check() && !download->space_checked, "asked again while the room is being read");
		fake_now += FREE_SPACE_TIMEOUT - 1;
		CHECK(download_space_check() && !download->space_checked, "and until the time given for it is out");
		fake_now += 1;
		CHECK(download_space_check() && download->space_checked && !fake_failures, "then it goes on without");
		fresh();
		download->space_needed = (190ULL << 20) + SPARE_BYTES;
		free_space_state = 1;
		fake_now += 2000;
		free_space_state = 2;
		free_space_bytes = 10ULL << 20;
		CHECK(!download_space_check() && fake_failures == 1, "a room that comes while it waits is used");
	} else CASE("room-no-offer-yet") {
		free_space_state = 2;
		free_space_bytes = 0;
		CHECK(download_space_check() && !fake_failures && !download->space_checked,
			"nothing is checked before the offer says how big the files are");
	} else CASE("extend") {
		struct download_file file;
		FILE *stream = tmpfile();
		long length;
		unsigned char buffer[0x8000];
		unsigned long index;

		memset(&file, 0, sizeof(file));
		file.stream = stream;
		CHECK(download_file_extend(&file, 0) && file.extent == 0, "a first piece at the start needs nothing");
		fwrite("abcd", 1, 4, stream);
		file.extent = 4;
		CHECK(download_file_extend(&file, 4) && file.extent == 4, "a piece at the end needs nothing");
		CHECK(download_file_extend(&file, 0x10000) && file.extent == 0x10000, "a piece past the end has the gap written");
		fseek(stream, 0, SEEK_END);
		length = ftell(stream);
		CHECK(length == 0x10000, "the file is as long as the piece's place: %ld", length);
		fseek(stream, 0, SEEK_SET);
		CHECK(fread(buffer, 1, 4, stream) == 4 && !memcmp(buffer, "abcd", 4), "what was written is kept");
		CHECK(fread(buffer, 1, 0x8000, stream) == 0x8000, "read the gap");
		for (index = 0; index < 0x8000 - 4; index++)
			CHECK(buffer[index] == 0, "the gap is zeros (%lu)", index);
		CHECK(download_file_extend(&file, 100) && file.extent == 0x10000, "a piece inside the file needs nothing");
		fclose(stream);
	} else CASE("sources") {
		CHECK(download_sources_allowed(TRUE) == 0, "over the internet the Wii U fetches from the host alone");
		CHECK(download_sources_allowed(FALSE) == WIIU_LAN_SOURCES, "on a LAN from a couple of machines besides");
	} else CASE("refusal") {
		char message[512];

		fake_map_download = 0;
		snprintf(message, sizeof(message), "You don't have the map hugeass.map.");
		map_share_download_refusal("custom_maps\\hugeass", 0x0e2b3ee4, message, sizeof(message));
		CHECK(strstr(message, "network.map_download"), "downloads off: %s", message);
		fake_map_download = 1;
		fake_tag_cache = 0;
		snprintf(message, sizeof(message), "You don't have the map hugeass.map.");
		map_share_download_refusal("custom_maps\\hugeass", 0x0e2b3ee4, message, sizeof(message));
		CHECK(strstr(message, "game.custom_edition"), "Custom Edition maps off: %s", message);
		fake_tag_cache = 1;
		snprintf(message, sizeof(message), "You don't have the map hugeass.map.");
		map_share_download_refusal("custom_maps\\hugeass", 0x0e2b3ee4, message, sizeof(message));
		CHECK(!strstr(message, "\r\n"), "nothing stopped it: nothing added: %s", message);
		fake_map_download = 0;
		snprintf(message, sizeof(message), "Your hugeass.map can't be played (debug.txt says why).");
		map_share_download_refusal("custom_maps\\hugeass", 0x0e2b3ee4, message, sizeof(message));
		CHECK(!strstr(message, "network.map_download"), "a map that cannot be played is not blamed on downloads: %s", message);
		fake_host = 1;
		snprintf(message, sizeof(message), "You don't have the map hugeass.map.");
		map_share_download_refusal("custom_maps\\hugeass", 0x0e2b3ee4, message, sizeof(message));
		CHECK(!strstr(message, "network.map_download"), "a host is not told: %s", message);
		fake_host = 0;
		snprintf(message, sizeof(message), "You don't have the map hugeass.map.");
		map_share_download_refusal("levels\\a10\\a10", 0, message, sizeof(message));
		CHECK(!strstr(message, "network.map_download"), "nor for the game's own map: %s", message);
	} else {
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
