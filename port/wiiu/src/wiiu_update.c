/*
WIIU_UPDATE.C

The Wii U's self-updater, its Cafe OS half: the decisions are wiiu_update_core.c's, the downloads wiiu_update_http.c's (both
tested on a desktop); this has the console's part of them, a worker thread on core 0, the panel the renderer draws at the main
menu, and the answers the controllers give it.

It is on only in a build made for releases: one with a build number (HALO_BUILD_NUMBER), the releases' repository
(HALO_UPDATE_REPOSITORY, "owner/repo") and the public key their manifests are signed with (HALO_UPDATE_PUBLIC_KEY, 64 hexadecimal
digits), which tools/wiiu_build.py takes from configure.py's environment. Without them it says so once in the log and does
nothing, not even look at the update folder: nothing leaves the console by default.

At the start, before the game: a swap or a trial left half done is resolved (wiiu_update_recover). Then, if update.auto allows
it, the worker asks GitHub for the latest release's manifest and signature (github.com/<repo>/releases/latest/download/..., which
uses no API quota), checks the signature, the build number and the maps' format, and once the main menu has the screen offers the
update: A updates, B is "not now", X twice is "never" (update.auto = false). The files are downloaded to update/<name>.part,
checked, and renamed to .new; then HOME is turned off, the running bundle is unmounted (RPXLoadingModule keeps it mounted until
the app ends: its RL_UnmountCurrentRunningBundle, reached through OSDynLoad as librpxloader does, without linking it), the files
are swapped (halo.wuhb last), HOME is turned on again, and the new build is started (RL_LaunchHomebrew, with the Wii U Menu if
that fails). The Homebrew on Menu plugin names the tile by the bundle's path, which stays the same.

The worker uses the network only while the shell allows it (wiiu_network_allowed: the foreground, not closing, not told to stop),
the card only in the foreground, and downloads only while the main menu has the screen; HOME drops the connection and keeps what
has arrived, and the download goes on from there with a Range request once the app is back. It looks at its flags at least every
250 ms, so that the shell's close, which waits for it 500 ms at most, does not wait in vain.

Not run on a console.
*/

#include "wiiu_update.h"

#include "port_config.h"
#include "wiiu_input.h"
#include "wiiu_shell.h"
#include "wiiu_update_core.h"
#include "wiiu_update_http.h"
#include "wiiu_update_roots.h"

#include <coreinit/dynload.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- what the build was made with */

#ifndef HALO_BUILD_NUMBER
#define HALO_BUILD_NUMBER 0
#endif
#ifdef HALO_UPDATE_REPOSITORY
#define REPOSITORY HALO_UPDATE_REPOSITORY
#else
#define REPOSITORY ""
#endif
#ifdef HALO_UPDATE_PUBLIC_KEY
#define PUBLIC_KEY HALO_UPDATE_PUBLIC_KEY
#else
#define PUBLIC_KEY ""
#endif
/* The format of the maps this build reads: the rebase tools/wiiu/convert_maps.py gives them (WIIU_REBASE: the memory window at
0x41EA0000 of halo_port_capacity.h, which this file cannot include, from the Xbox's 0x80000000). tools/test_wiiu_update.py checks
that the three agree. */
#define MAPS_FORMAT "c1ea0000"

#define TEXT_(x) #x
#define TEXT(x) TEXT_(x)

/* What the release script (tools/wiiu/make_wiiu_release.py) reads out of the bundle before it signs a release: that it is the build
the manifest names, reads the same maps, and trusts the key that signs it. */
__attribute__((used)) static const char build_marker[] =
	"OpenCE Wii U update: build=" TEXT(HALO_BUILD_NUMBER) " maps=" MAPS_FORMAT " repository=" REPOSITORY " key=" PUBLIC_KEY ";";

/* the bundle's path from the card's root, as RPXLoadingModule names it */
#define BUNDLE_PATH "wiiu/apps/halo/" WIIU_UPDATE_BUNDLE
#define CARD_ROOT "/vol/external01"
/* room left on the card besides the release (the logs, the cache files the game makes, a save) */
#define SPARE_BYTES (32u * 1024u * 1024u)
#define WORKER_STACK_BYTES (128u * 1024u)
#define PANEL_BYTES 704u

/* ---------- state */

static wiiu_update_journal journal;
static wiiu_update_fs card;
static FSAClientHandle fsa_client = -1;
static int fsa_started;

/* RPXLoadingModule's exports (wiiu-env/RPXLoadingModule, through librpxloader's names); RPX_LOADER_RESULT_SUCCESS is 0 */
static struct
{
	int found;
	uint32_t version;
	int32_t (*unmount)(void);
	int32_t (*launch)(const char *path);
	int32_t (*path)(char *out, uint32_t size);
	char running[256];
} loader;

static OSThread worker_thread;
static uint8_t worker_stack[WORKER_STACK_BYTES] __attribute__((aligned(16)));
static volatile uint32_t worker_started, worker_done, stop_asked;

static wiiu_update_manifest offer;
static int offer_ready;
static uint8_t offer_digest[32];
static uint8_t offer_unchanged[WIIU_UPDATE_FILES];

/* the panel: its text, a count of its changes, and whether it takes the controllers' face buttons (every panel does: the menu under it
must not move while it is up) */
static pthread_mutex_t panel_lock = PTHREAD_MUTEX_INITIALIZER;
static char panel_text[PANEL_BYTES];
static volatile uint32_t panel_generation;
static volatile uint32_t panel_up;
static volatile uint32_t panel_takes_input;

/* the controllers' answers: presses counted by the game thread, taken by the worker */
enum
{
	ANSWER_A,
	ANSWER_B,
	ANSWER_X,
	ANSWER_Y,
	ANSWERS
};
static volatile uint32_t presses[ANSWERS];
static uint32_t presses_taken[ANSWERS];
static unsigned pad_held[WIIU_PAD_PORTS];
static uint8_t pad_kept[WIIU_PAD_PORTS];

/* the game thread's view of the menus: now, and whether the main menu has had the screen this run */
static volatile uint32_t main_menu_now, menus_now, main_menu_reached;

static uint32_t load(volatile uint32_t *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store(volatile uint32_t *value, uint32_t to)
{
	__atomic_store_n(value, to, __ATOMIC_RELEASE);
}

static void sleep_ms(uint32_t milliseconds)
{
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
}

/* ---------- the card */

static int card_exists(void *context, const char *path)
{
	struct stat information;

	(void)context;
	return stat(path, &information) == 0;
}

static int card_rename(void *context, const char *from, const char *to)
{
	(void)context;
	return rename(from, to);
}

static int card_remove(void *context, const char *path)
{
	(void)context;
	return unlink(path);
}

static int card_write(void *context, const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int written;

	(void)context;
	if (!file)
		return -1;
	written = fwrite(data, 1, size, file) == size && fflush(file) == 0 && fsync(fileno(file)) == 0;
	if (fclose(file) != 0)
		written = 0;
	return written ? 0 : -1;
}

static long card_read(void *context, const char *path, void *data, size_t capacity)
{
	FILE *file = fopen(path, "rb");
	size_t got;

	(void)context;
	if (!file)
		return -1;
	got = fread(data, 1, capacity, file);
	fclose(file);
	return (long)got;
}

/* (the folder's entries: the renames are on the card before the new build is started) */
static int card_flush(void *context)
{
	(void)context;
	return fsa_client >= 0 && FSAFlushVolume(fsa_client, CARD_ROOT) >= 0 ? 0 : -1;
}

static void core_log(void *context, const char *text)
{
	(void)context;
	wiiu_log_text(text);
}

static int unmount_bundle(void *context)
{
	int32_t result;

	(void)context;
	if (!loader.unmount)
		return -1;
	result = loader.unmount();
	wiiu_log("Update: RL_UnmountCurrentRunningBundle -> %d", (int)result);
	return result == 0 ? 0 : -1;
}

/* the new build (or the old one, put back) started in place of this one, at the path this one was started from (RPXLoadingModule's are
from the card's root); the Wii U Menu if RPXLoadingModule cannot */
static void relaunch(void)
{
	int32_t result = -1;

	if (loader.launch)
		result = loader.launch(BUNDLE_PATH);
	wiiu_log("Update: RL_LaunchHomebrew(" BUNDLE_PATH ") -> %d", (int)result);
	if (result != 0)
		wiiu_launch_menu();
}

/* a path RPXLoadingModule gives, from the card's root ("wiiu/apps/halo/halo.wuhb"), whatever prefix it is written with */
static const char *from_card_root(const char *path)
{
	static const char *const prefixes[] = { "fs:/vol/external01/", "/vol/external01/", "sd:/", "/" };
	unsigned i;

	for (i = 0; i < sizeof(prefixes) / sizeof(*prefixes); i++)
		if (!strncasecmp(path, prefixes[i], strlen(prefixes[i])))
			return path + strlen(prefixes[i]);
	return path;
}

static void system_for_core(wiiu_update_system *system, int crash_note)
{
	memset(system, 0, sizeof(*system));
	system->running_build = (uint32_t)HALO_BUILD_NUMBER;
	system->crash_note = crash_note;
	system->unmount = loader.found ? unmount_bundle : NULL;
	system->log = core_log;
}

/* ---------- the RPX loader */

static void loader_find(void)
{
	OSDynLoad_Module module = NULL;
	int32_t (*get_version)(uint32_t *version) = NULL;

	if (OSDynLoad_Acquire("homebrew_rpx_loader", &module) != OS_DYNLOAD_OK || !module)
	{
		wiiu_log("Update: RPXLoadingModule is not loaded (not Aroma?): updates are off");
		return;
	}
	if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "RL_GetVersion", (void **)&get_version) != OS_DYNLOAD_OK || !get_version ||
		get_version(&loader.version) != 0)
	{
		wiiu_log("Update: RPXLoadingModule does not say its version: updates are off");
		return;
	}
	if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "RL_UnmountCurrentRunningBundle", (void **)&loader.unmount) != OS_DYNLOAD_OK)
		loader.unmount = NULL;
	if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "RL_LaunchHomebrew", (void **)&loader.launch) != OS_DYNLOAD_OK)
		loader.launch = NULL;
	if (loader.version < 2 || OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "RL_GetPathOfRunningExecutable", (void **)&loader.path) != OS_DYNLOAD_OK)
		loader.path = NULL;
	if (loader.path && loader.path(loader.running, sizeof(loader.running) - 1) == 0)
		loader.running[sizeof(loader.running) - 1] = 0;
	else
		loader.running[0] = 0;
	wiiu_log("Update: RPXLoadingModule version %u: unmount %s, launch %s; running %s", (unsigned)loader.version, loader.unmount ? "yes" : "no",
		loader.launch ? "yes" : "no", loader.running[0] ? loader.running : "(not said)");
	loader.found = loader.version >= 1 && loader.unmount && loader.launch;
}

/* ---------- the panel */

static void panel_show(int takes_input, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void panel_show(int takes_input, const char *format, ...)
{
	va_list arguments;

	pthread_mutex_lock(&panel_lock);
	va_start(arguments, format);
	vsnprintf(panel_text, sizeof(panel_text), format, arguments);
	va_end(arguments);
	__atomic_add_fetch(&panel_generation, 1u, __ATOMIC_ACQ_REL);
	pthread_mutex_unlock(&panel_lock);
	store(&panel_takes_input, takes_input ? 1u : 0u);
	store(&panel_up, 1u);
}

static void panel_hide(void)
{
	store(&panel_takes_input, 0u);
	store(&panel_up, 0u);
}

int wiiu_update_panel(char *text, unsigned capacity, unsigned *generation)
{
	unsigned now;

	if (!load(&panel_up) || !load(&main_menu_now))
		return 0;
	now = load(&panel_generation);
	if (now != *generation && capacity)
	{
		pthread_mutex_lock(&panel_lock);
		snprintf(text, capacity, "%s", panel_text);
		now = load(&panel_generation);
		pthread_mutex_unlock(&panel_lock);
		*generation = now;
	}
	return 1;
}

int wiiu_update_panel_input(unsigned port, unsigned buttons)
{
	const unsigned answers = WIIU_PAD_A | WIIU_PAD_B | WIIU_PAD_X | WIIU_PAD_Y;
	unsigned pressed;

	if (port >= WIIU_PAD_PORTS)
		return 0;
	pressed = buttons & ~pad_held[port];
	pad_held[port] = buttons;
	if (load(&panel_takes_input) && load(&panel_up) && load(&main_menu_now))
	{
		/* (presses, not holds: a button held as the panel opened does not answer it) */
		if (pressed & WIIU_PAD_A)
			__atomic_add_fetch(&presses[ANSWER_A], 1u, __ATOMIC_ACQ_REL);
		if (pressed & WIIU_PAD_B)
			__atomic_add_fetch(&presses[ANSWER_B], 1u, __ATOMIC_ACQ_REL);
		if (pressed & WIIU_PAD_X)
			__atomic_add_fetch(&presses[ANSWER_X], 1u, __ATOMIC_ACQ_REL);
		if (pressed & WIIU_PAD_Y)
			__atomic_add_fetch(&presses[ANSWER_Y], 1u, __ATOMIC_ACQ_REL);
		pad_kept[port] = 1;
		return 1;
	}
	/* the button that closed the panel is kept from the menu under it until it is let go */
	if (pad_kept[port])
	{
		if (buttons & answers)
			return 1;
		pad_kept[port] = 0;
	}
	return 0;
}

void wiiu_update_menus(int main_menu, int menus)
{
	store(&main_menu_now, main_menu ? 1u : 0u);
	store(&menus_now, menus ? 1u : 0u);
	if (main_menu)
		store(&main_menu_reached, 1u);
}

/* a press of one of the answers since the worker last looked (the older ones forgotten) */
static void presses_forget(void)
{
	unsigned i;

	for (i = 0; i < ANSWERS; i++)
		presses_taken[i] = load(&presses[i]);
}

static int pressed(int answer)
{
	const uint32_t now = load(&presses[answer]);

	if (now == presses_taken[answer])
		return 0;
	presses_taken[answer] = now;
	return 1;
}

/* ---------- when the worker may do what */

static int closing(void)
{
	return load(&stop_asked) || wiiu_exit_requested();
}

static int may_use_network(void)
{
	return !closing() && wiiu_network_allowed() && wiiu_in_foreground();
}

static int at_main_menu(void)
{
	return !closing() && wiiu_in_foreground() && load(&main_menu_now) && load(&menus_now);
}

/* the card is written only in the foreground: waits for it; 0 if the app closes first */
static int foreground_or_closing(void)
{
	while (!closing() && !wiiu_in_foreground())
		sleep_ms(50);
	return !closing();
}

static int fetch_stop(void *context)
{
	(void)context;
	return !may_use_network();
}

static volatile uint32_t download_cancelled;

static int download_stop(void *context)
{
	(void)context;
	return closing() || load(&download_cancelled);
}

/* the card is written, and the network used, only in the foreground and with the main menu up */
static int download_may_write(void *context)
{
	(void)context;
	return may_use_network() && at_main_menu();
}

/* ---------- checking for an update */

static wiiu_http *http;

static int http_ready(void)
{
	char error[160];

	if (http)
		return 1;
	http = wiiu_http_create(wiiu_update_roots, wiiu_update_root_sizes, WIIU_UPDATE_ROOT_COUNT, error, sizeof(error));
	if (!http)
		wiiu_log("Update: HTTPS cannot be used: %s", error);
	return http != NULL;
}

/* 1 if an update is to be offered (in offer), 0 if not, -1 if it could not be found out (the network: tried again later) */
static int check_for_update(void)
{
	static char manifest_text[WIIU_UPDATE_MANIFEST_MOST];
	char url[256], reason[200];
	uint8_t signature[65], key[32];
	size_t manifest_size = 0, signature_size = 0;
	wiiu_http_result result;
	OSTime started = OSGetSystemTime();
	uint32_t i;

	if (!http_ready())
		return 0;
	snprintf(url, sizeof(url), "https://github.com/" REPOSITORY "/releases/latest/download/" WIIU_UPDATE_MANIFEST_NAME);
	if (wiiu_http_fetch(http, url, manifest_text, sizeof(manifest_text), &manifest_size, fetch_stop, NULL, &result) != 0)
	{
		wiiu_log("Update: the latest release's manifest could not be fetched (%s; status %d after %d redirects)", result.error, result.status,
			result.redirects);
		return result.status == 404 ? 0 : -1;
	}
	snprintf(url, sizeof(url), "https://github.com/" REPOSITORY "/releases/latest/download/" WIIU_UPDATE_SIGNATURE_NAME);
	if (wiiu_http_fetch(http, url, signature, sizeof(signature), &signature_size, fetch_stop, NULL, &result) != 0)
	{
		wiiu_log("Update: the manifest's signature could not be fetched (%s; status %d)", result.error, result.status);
		return result.status == 404 ? 0 : -1;
	}
	wiiu_log("Update: the manifest (%u bytes) and its signature fetched in %u ms", (unsigned)manifest_size,
		(unsigned)OSTicksToMilliseconds(OSGetSystemTime() - started));
	if (wiiu_update_key_from_hex(PUBLIC_KEY, key) != 0 || !wiiu_update_signature_valid(key, signature, signature_size, manifest_text, manifest_size))
	{
		wiiu_log("Update: the latest release's manifest is not signed with this build's key: it is ignored");
		return 0;
	}
	if (wiiu_update_manifest_parse(manifest_text, manifest_size, &offer, reason, sizeof(reason)) != 0)
	{
		wiiu_log("Update: the latest release's manifest is refused: %s", reason);
		return 0;
	}
	wiiu_http_sha256(manifest_text, manifest_size, offer_digest);
	if (offer.build <= (uint32_t)HALO_BUILD_NUMBER)
	{
		wiiu_log("Update: this is the latest build (%u; the latest release is %u)", (unsigned)HALO_BUILD_NUMBER, (unsigned)offer.build);
		return 0;
	}
	/* (the last update settled first: a trial kept or not, its old files deleted, at the next start) */
	if (journal.state != WIIU_UPDATE_IDLE && journal.state != WIIU_UPDATE_DOWNLOADING && journal.state != WIIU_UPDATE_STAGED)
	{
		wiiu_log("Update: build %u is available; it is offered once build %u's update is settled, at the next start", (unsigned)offer.build,
			(unsigned)HALO_BUILD_NUMBER);
		return 0;
	}
	if (offer.build == journal.rejected)
	{
		wiiu_log("Update: build %u failed its trial here before: it is not offered again", (unsigned)offer.build);
		return 0;
	}
	if (strcmp(offer.maps, MAPS_FORMAT))
	{
		wiiu_log("Update: build %u reads maps converted for %s, and this card's are for %s: it is not offered (convert the maps again and "
			"install it by hand)", (unsigned)offer.build, offer.maps, MAPS_FORMAT);
		return 0;
	}
	/* (a file of the release the folder already has, byte for byte, is left as it is: the brokers' list, a shader pack that did not change) */
	for (i = 0; i < offer.file_count; i++)
	{
		char path[WIIU_UPDATE_PATH_BYTES];

		offer_unchanged[i] = strcmp(offer.files[i].name, WIIU_UPDATE_BUNDLE) &&
			wiiu_update_path(&card, offer.files[i].name, WIIU_UPDATE_CURRENT, path, sizeof(path)) == 0 &&
			wiiu_http_file_matches(path, offer.files[i].size, offer.files[i].sha256, fetch_stop, NULL) == 1;
	}
	return 1;
}

/* ---------- updating */

static uint64_t bytes_to_download(void)
{
	uint64_t total = 0;
	uint32_t i;

	for (i = 0; i < journal.entry_count; i++)
	{
		char path[WIIU_UPDATE_PATH_BYTES];
		struct stat information;
		uint64_t have = 0;

		if (wiiu_update_path(&card, journal.entries[i].name, WIIU_UPDATE_NEW, path, sizeof(path)) == 0 && stat(path, &information) == 0)
			continue;
		if (wiiu_update_path(&card, journal.entries[i].name, WIIU_UPDATE_PART, path, sizeof(path)) == 0 && stat(path, &information) == 0)
			have = (uint64_t)information.st_size;
		total += have < journal.entries[i].size ? journal.entries[i].size - have : 0;
	}
	return total;
}

/* A message with only A to answer it, until it is answered or the app closes. The automatic answer (debug.update_answer) does not
wait. */
static void tell_and_wait(const char *answer, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void tell_and_wait(const char *answer, const char *format, ...)
{
	char message[200];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	wiiu_log("Update: %s", message);
	if (answer[0])
		return;
	presses_forget();
	panel_show(1, "HALO UPDATE\n%s\n\nA: OK", message);
	while (!closing() && !pressed(ANSWER_A))
		sleep_ms(50);
	panel_hide();
}

typedef struct progress
{
	const char *name;
	uint32_t index, count;
	OSTime shown;
} progress;

static progress download_progress;

static void download_progress_shown(void *context, uint32_t done, uint32_t size)
{
	progress *p = context;
	const OSTime now = OSGetSystemTime();
	char bar[61];
	unsigned filled = size ? (unsigned)((uint64_t)done * 60u / size) : 0, i;

	/* (the panel at most four times a second: each change is a texture written again) */
	if (done < size && p->shown && now - p->shown < OSMillisecondsToTicks(250))
		return;
	p->shown = now;
	for (i = 0; i < 60; i++)
		bar[i] = i < filled ? '#' : '-';
	bar[60] = 0;
	panel_show(1, "HALO UPDATE\nDOWNLOADING BUILD %u: %s (%u OF %u)\n%u.%u OF %u.%u MB\n[%s]\nB: STOP (IT GOES ON FROM HERE NEXT TIME)",
		(unsigned)journal.to_build, p->name, (unsigned)p->index, (unsigned)p->count, (unsigned)(done >> 20), (unsigned)((done % 1048576u) * 10u / 1048576u),
		(unsigned)(size >> 20), (unsigned)((size % 1048576u) * 10u / 1048576u), bar);
}

static void watch_for_cancel(void)
{
	if (pressed(ANSWER_B))
		store(&download_cancelled, 1u);
}

static int download_stop_watching(void *context)
{
	watch_for_cancel();
	return download_stop(context);
}

/* Downloads, checks, stages and puts in place the offered release; returns only if it did not start the new build. */
static void update_now(const char *answer)
{
	wiiu_update_system system;
	uint64_t needed, free_bytes = 0;
	uint32_t i;
	int committed;

	/* the journal for this release: the parts of an earlier attempt at the same manifest go on, any others are deleted */
	if (!((journal.state == WIIU_UPDATE_DOWNLOADING || journal.state == WIIU_UPDATE_STAGED) && journal.has_manifest &&
		!memcmp(journal.manifest_sha256, offer_digest, sizeof(offer_digest)) && journal.to_build == offer.build))
	{
		if ((journal.state == WIIU_UPDATE_DOWNLOADING || journal.state == WIIU_UPDATE_STAGED) && foreground_or_closing())
			wiiu_update_discard(&card, &journal);
		wiiu_update_journal_entries(&journal, &offer, offer_unchanged);
		journal.to_build = offer.build;
		journal.from_build = (uint32_t)HALO_BUILD_NUMBER;
		memcpy(journal.manifest_sha256, offer_digest, sizeof(offer_digest));
		journal.has_manifest = 1;
	}
	if (journal.state != WIIU_UPDATE_STAGED)
	{
		panel_show(1, "HALO UPDATE\nLOOKING AT THE SD CARD'S FREE SPACE...");
		needed = bytes_to_download() + SPARE_BYTES;
		{
			const OSTime started = OSGetSystemTime();
			const FSError error = fsa_client >= 0 ? FSAGetFreeSpaceSize(fsa_client, CARD_ROOT, &free_bytes) : FS_ERROR_NOT_INIT;

			wiiu_log("Update: the card's free space read in %u ms: %s, %llu MB (%llu MB needed)",
				(unsigned)OSTicksToMilliseconds(OSGetSystemTime() - started), error >= 0 ? "ok" : FSAGetStatusStr(error),
				(unsigned long long)(free_bytes >> 20), (unsigned long long)(needed >> 20));
			if (error < 0)
			{
				tell_and_wait(answer, "THE SD CARD'S FREE SPACE CANNOT BE READ: NOTHING WAS CHANGED.");
				return;
			}
		}
		if (free_bytes < needed)
		{
			tell_and_wait(answer, "NOT ENOUGH ROOM ON THE SD CARD: %u MB MORE IS NEEDED.", (unsigned)((needed - free_bytes + 1048575u) >> 20));
			return;
		}
		journal.state = WIIU_UPDATE_DOWNLOADING;
		if (!foreground_or_closing())
			return;
		if (wiiu_update_journal_save(&card, &journal) != 0)
		{
			tell_and_wait(answer, "THE SD CARD CANNOT BE WRITTEN: NOTHING WAS CHANGED.");
			return;
		}
		store(&download_cancelled, 0u);
		presses_forget();
		for (i = 0; i < journal.entry_count; i++)
		{
			const wiiu_update_entry *entry = &journal.entries[i];
			char url[320], part[WIIU_UPDATE_PATH_BYTES], staged[WIIU_UPDATE_PATH_BYTES];
			wiiu_http_download_hooks hooks;
			wiiu_http_result result;
			int attempt, got = WIIU_HTTP_INCOMPLETE;

			if (wiiu_update_path(&card, entry->name, WIIU_UPDATE_PART, part, sizeof(part)) != 0 ||
				wiiu_update_path(&card, entry->name, WIIU_UPDATE_NEW, staged, sizeof(staged)) != 0)
				return;
			if (wiiu_http_file_matches(staged, entry->size, entry->sha256, download_stop, NULL) == 1)
				continue;
			snprintf(url, sizeof(url), "https://github.com/" REPOSITORY "/releases/download/wiiu-%u/%s", (unsigned)journal.to_build, entry->name);
			memset(&download_progress, 0, sizeof(download_progress));
			download_progress.name = entry->name;
			download_progress.index = i + 1;
			download_progress.count = journal.entry_count;
			memset(&hooks, 0, sizeof(hooks));
			hooks.context = &download_progress;
			hooks.stop = download_stop_watching;
			hooks.may_write = download_may_write;
			hooks.progress = download_progress_shown;
			/* (a file that arrives damaged is fetched once more from its start) */
			for (attempt = 0; attempt < 2; attempt++)
			{
				const OSTime started = OSGetSystemTime();

				got = wiiu_http_download(http, url, part, staged, entry->size, entry->sha256, &hooks, &result);
				wiiu_log("Update: %s: %s after %u ms (%s%s)", entry->name, got == WIIU_HTTP_DOWNLOADED ? "downloaded" : got == WIIU_HTTP_DAMAGED ? "damaged" :
					got == WIIU_HTTP_CARD_ERROR ? "the card failed" : "not finished", (unsigned)OSTicksToMilliseconds(OSGetSystemTime() - started),
					result.error[0] ? result.error : "ok", result.stopped ? ", stopped" : "");
				if (got != WIIU_HTTP_DAMAGED)
					break;
			}
			if (got == WIIU_HTTP_DOWNLOADED)
				continue;
			panel_hide();
			if (closing())
				return;
			if (load(&download_cancelled))
			{
				wiiu_log("Update: the download was stopped by the player; it goes on from here next time");
				return;
			}
			tell_and_wait(answer, got == WIIU_HTTP_DAMAGED ? "THE DOWNLOAD ARRIVED DAMAGED TWICE: NOTHING WAS CHANGED." :
				got == WIIU_HTTP_CARD_ERROR ? "THE SD CARD COULD NOT BE WRITTEN: NOTHING WAS CHANGED." :
				"THE DOWNLOAD STOPPED. IT GOES ON FROM HERE NEXT TIME.");
			return;
		}
		journal.state = WIIU_UPDATE_STAGED;
		if (!foreground_or_closing())
			return;
		if (wiiu_update_journal_save(&card, &journal) != 0)
		{
			tell_and_wait(answer, "THE SD CARD CANNOT BE WRITTEN: NOTHING WAS CHANGED.");
			return;
		}
	}
	/* (each staged file read back: what the card holds is what was checked) */
	panel_show(1, "HALO UPDATE\nCHECKING BUILD %u'S FILES...", (unsigned)journal.to_build);
	for (i = 0; i < journal.entry_count; i++)
	{
		char staged[WIIU_UPDATE_PATH_BYTES];
		int matches;

		if (wiiu_update_path(&card, journal.entries[i].name, WIIU_UPDATE_NEW, staged, sizeof(staged)) != 0)
		{
			panel_hide();
			return;
		}
		matches = wiiu_http_file_matches(staged, journal.entries[i].size, journal.entries[i].sha256, download_stop, NULL);
		/* (stopped while it was read: the release stays staged, for the next time) */
		if (matches < 0)
		{
			panel_hide();
			return;
		}
		if (!matches)
		{
			if (!foreground_or_closing())
				return;
			remove(staged);
			journal.state = WIIU_UPDATE_DOWNLOADING;
			wiiu_update_journal_save(&card, &journal);
			panel_hide();
			tell_and_wait(answer, "A DOWNLOADED FILE IS DAMAGED: IT IS FETCHED AGAIN NEXT TIME.");
			return;
		}
	}
	if (closing() || !wiiu_in_foreground())
	{
		panel_hide();
		return;
	}
	/* HOME off while the files move: a swap the system cuts off is resolved at the next start, but one not cut off is quicker */
	panel_show(1, "HALO UPDATE\nPUTTING BUILD %u IN PLACE. DO NOT TURN THE CONSOLE OFF.", (unsigned)journal.to_build);
	OSEnableHomeButtonMenu(FALSE);
	system_for_core(&system, 0);
	committed = wiiu_update_commit(&card, &journal, &system);
	OSEnableHomeButtonMenu(TRUE);
	if (committed == 1)
	{
		panel_show(1, "HALO UPDATE\nBUILD %u IS IN PLACE: STARTING IT...", (unsigned)journal.to_build);
		relaunch();
		/* (the system closes the app: the panel stays until it does) */
		while (!closing())
			sleep_ms(50);
		return;
	}
	panel_hide();
	tell_and_wait(answer, committed == 0 ? "THE FILES COULD NOT BE PUT IN PLACE: NOTHING WAS CHANGED." :
		"SOME FILES COULD NOT BE MOVED: THE NEXT START SORTS IT OUT.");
}

/* the offer at the main menu: A, B, or X twice */
static void offer_update(void)
{
	char answer[16];
	uint64_t total = 0;
	uint32_t i;
	int confirming = 0;

	/* (a copy: writing config.toml may move the setting's text) */
	snprintf(answer, sizeof(answer), "%s", config_string("debug.update_answer"));
	for (i = 0; i < offer.file_count; i++)
		if (!offer_unchanged[i])
			total += offer.files[i].size;
	if (answer[0])
	{
		wiiu_log("Update: build %u is available: answering \"%s\" (debug.update_answer)", (unsigned)offer.build, answer);
		if (!strcmp(answer, "yes"))
			update_now(answer);
		else if (!strcmp(answer, "never") && foreground_or_closing() && config_write_boolean("update.auto", 0))
			wiiu_update_discard(&card, &journal);
		return;
	}
	wiiu_log("Update: build %u is offered (%llu bytes to download)", (unsigned)offer.build, (unsigned long long)total);
	for (;;)
	{
		if (closing())
		{
			panel_hide();
			return;
		}
		if (!confirming)
			panel_show(1, "HALO UPDATE\nBUILD %u IS AVAILABLE. THIS IS BUILD %u.\n%u.%u MB TO DOWNLOAD. THE GAME THEN RESTARTS BY ITSELF.\n\n"
				"A: UPDATE NOW    B: NOT NOW    X TWICE: NEVER ASK AGAIN", (unsigned)offer.build, (unsigned)HALO_BUILD_NUMBER,
				(unsigned)(total >> 20), (unsigned)((total % 1048576u) * 10u / 1048576u));
		else
			panel_show(1, "HALO UPDATE\nSTOP CHECKING FOR UPDATES?\nCHECK FOR UPDATES IN NETWORK SETUP TURNS IT BACK ON.\n\nX: YES, NEVER ASK AGAIN    B: BACK");
		/* (a press made for the screen before is not this one's answer) */
		presses_forget();
		while (!closing())
		{
			/* (each press taken whichever screen is up, so that an A pressed on the question "never?" does not update on the way back) */
			const int a = pressed(ANSWER_A), b = pressed(ANSWER_B), x = pressed(ANSWER_X);

			if (!confirming && a)
			{
				wiiu_log("Update: the player chose to update to build %u", (unsigned)offer.build);
				update_now("");
				return;
			}
			if (b)
			{
				if (!confirming)
				{
					wiiu_log("Update: not now (build %u is offered again at the next start)", (unsigned)offer.build);
					panel_hide();
					return;
				}
				confirming = 0;
				break;
			}
			if (x)
			{
				if (!confirming)
				{
					confirming = 1;
					break;
				}
				panel_hide();
				if (!foreground_or_closing())
					return;
				wiiu_log("Update: never: %s", config_write_boolean("update.auto", 0) ? "update.auto = false written to config.toml" :
					"update.auto could not be written");
				wiiu_update_discard(&card, &journal);
				return;
			}
			sleep_ms(50);
		}
	}
}

/* the player told that a build was set aside: A, or Y to have it offered again */
static void tell_notice(void)
{
	const uint32_t build = journal.notice;

	presses_forget();
	panel_show(1, "HALO UPDATE\nBUILD %u DID NOT START PROPERLY, SO BUILD %u IS BACK.\nBUILD %u WILL NOT BE OFFERED AGAIN; A LATER BUILD WILL BE.\n\n"
		"A: OK    Y: OFFER BUILD %u AGAIN", (unsigned)build, (unsigned)HALO_BUILD_NUMBER, (unsigned)build, (unsigned)build);
	while (!closing())
	{
		if (pressed(ANSWER_A))
			break;
		if (pressed(ANSWER_Y))
		{
			wiiu_log("Update: build %u is to be offered again", (unsigned)build);
			journal.rejected = 0;
			break;
		}
		sleep_ms(50);
	}
	panel_hide();
	if (!foreground_or_closing())
		return;
	journal.notice = 0;
	wiiu_update_journal_save(&card, &journal);
}

/* ---------- the worker */

static int checks_wanted;

static int worker_main(int argc, const char **argv)
{
	int checked = 0, check_failures = 0;
	OSTime retry_at = 0;

	(void)argc;
	(void)argv;
	wiiu_tag_thread("update");
	while (!closing())
	{
		/* (a trial build reached its main menu: it is kept) */
		if (journal.state == WIIU_UPDATE_TRIAL && load(&main_menu_reached) && wiiu_in_foreground())
		{
			if (wiiu_update_mark_good(&card, &journal) == 0)
				wiiu_log("Update: build %u reached its main menu: it is kept (build %u's files are deleted at the next start)", (unsigned)journal.to_build,
					(unsigned)journal.from_build);
		}
		if (checks_wanted && !checked && may_use_network() && OSGetSystemTime() >= retry_at)
		{
			const int found = check_for_update();

			if (found >= 0 || ++check_failures >= 2)
				checked = 1;
			else
				retry_at = OSGetSystemTime() + OSSecondsToTicks(30);
			offer_ready = found > 0;
			/* (the parts of a release that is no longer the one to install) */
			if (!offer_ready && (journal.state == WIIU_UPDATE_DOWNLOADING || journal.state == WIIU_UPDATE_STAGED) && found == 0 &&
				foreground_or_closing())
				wiiu_update_discard(&card, &journal);
		}
		if (at_main_menu())
		{
			if (journal.notice)
				tell_notice();
			else if (offer_ready)
			{
				offer_ready = 0;
				offer_update();
			}
		}
		if (journal.state != WIIU_UPDATE_TRIAL && !journal.notice && !offer_ready && (!checks_wanted || checked))
			break;
		sleep_ms(50);
	}
	if (http)
	{
		wiiu_http_destroy(http);
		http = NULL;
	}
	wiiu_untag_thread();
	store(&worker_done, 1u);
	return 0;
}

/* ---------- the shell's */

/* why the updater is off, or NULL if it is on */
static const char *off_reason(void)
{
	uint8_t key[32];

	if (HALO_BUILD_NUMBER <= 0)
		return "this build has no build number (HALO_BUILD_NUMBER)";
	if (!REPOSITORY[0] || !strchr(REPOSITORY, '/'))
		return "this build names no releases' repository (HALO_UPDATE_REPOSITORY)";
	if (wiiu_update_key_from_hex(PUBLIC_KEY, key) != 0)
		return "this build has no public key to check releases with (HALO_UPDATE_PUBLIC_KEY)";
	return NULL;
}

int wiiu_update_startup(int crash_note)
{
	const char *off = off_reason();
	wiiu_update_system system;
	int recovered;

	/* (the build's record of itself first, whether or not the updater is on: the log says what any build would trust, and the record is
	then in every bundle's RPX, where the release script reads it) */
	wiiu_log("Update: %s", build_marker);
	if (off)
	{
		wiiu_log("Update: off: %s", off);
		return 0;
	}
	card.context = NULL;
	card.root = WIIU_DATA_DIRECTORY;
	card.exists = card_exists;
	card.rename = card_rename;
	card.remove = card_remove;
	card.write = card_write;
	card.read = card_read;
	card.flush = card_flush;
	mkdir(WIIU_DATA_DIRECTORY "/" WIIU_UPDATE_FOLDER, 0755);
	if (!fsa_started)
	{
		fsa_started = 1;
		FSAInit();
		fsa_client = FSAAddClient(NULL);
		if (fsa_client < 0)
			wiiu_log("Update: no FSA client (%d): the card's free space cannot be read", (int)fsa_client);
	}
	loader_find();
	if (loader.found && loader.running[0] && strcasecmp(from_card_root(loader.running), BUNDLE_PATH))
	{
		wiiu_log("Update: the app runs from %s, not %s: updates are off", loader.running, BUNDLE_PATH);
		loader.found = 0;
	}
	if (wiiu_update_journal_load(&card, &journal))
		wiiu_log("Update: the journal says %s (build %u from %u, start %u; rejected %u)", wiiu_update_state_name(journal.state),
			(unsigned)journal.to_build, (unsigned)journal.from_build, (unsigned)journal.starts, (unsigned)journal.rejected);
	system_for_core(&system, crash_note);
	recovered = wiiu_update_recover(&card, &journal, &system);
	if (recovered == WIIU_UPDATE_RELAUNCH)
	{
		relaunch();
		return 1;
	}
	if (recovered < 0)
	{
		wiiu_log("Update: the journal could not be followed: updates are off for this run (the files are as the log says)");
		return 0;
	}
	/* (no looking unless the player allows it, the sockets are on, someone is there to answer, and the bundle can be swapped) */
	checks_wanted = loader.found && config_boolean("update.auto") && config_boolean("network.wiiu_sockets") &&
		(config_string("debug.update_answer")[0] || (config_real("debug.exit_after") <= 0.0 && !config_string("debug.network_test")[0]));
	if (!checks_wanted)
		wiiu_log("Update: no check this run (update.auto %s, network.wiiu_sockets %s, the RPX loader %s)", config_boolean("update.auto") ? "on" : "off",
			config_boolean("network.wiiu_sockets") ? "on" : "off", loader.found ? "found" : "not found");
	if (journal.state != WIIU_UPDATE_TRIAL && !journal.notice && !checks_wanted)
		return 0;
	{
		int32_t priority = OSGetThreadPriority(OSGetCurrentThread()) + 6;

		if (priority > 31)
			priority = 31;
		if (!OSCreateThread(&worker_thread, worker_main, 0, NULL, worker_stack + sizeof(worker_stack), sizeof(worker_stack), priority,
				(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED)))
		{
			wiiu_log("Update: OSCreateThread failed for the worker");
			return 0;
		}
		OSSetThreadName(&worker_thread, "update");
		store(&worker_started, 1u);
		OSResumeThread(&worker_thread);
	}
	return 0;
}

int wiiu_update_stop(unsigned wait_ms)
{
	unsigned waited = 0;

	store(&stop_asked, 1u);
	if (!load(&worker_started))
		return 1;
	while (!load(&worker_done) && waited < wait_ms)
	{
		sleep_ms(10);
		waited += 10u;
	}
	if (!load(&worker_done))
		wiiu_log("Update: the worker has not stopped after %u ms", wait_ms);
	return load(&worker_done) != 0;
}
