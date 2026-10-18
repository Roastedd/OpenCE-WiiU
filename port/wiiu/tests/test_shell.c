/*
The Wii U shell's portable parts (port/wiiu/src/wiiu_shell_*.c): the log queue and its worker, the watch (stall and close
decisions), the black box. Built for the desktop, for 32-bit x86 and for 32-bit big-endian PowerPC (run under qemu-ppc), with
the undefined-behaviour sanitizer where there is one: tools/wiiu/shell_tests.sh in the repository this port is developed in.
Exit status 0 when everything holds; a failing check says which and exits 1.
*/

#include "../src/wiiu_shell_blackbox.h"
#include "../src/wiiu_shell_log.h"
#include "../src/wiiu_shell_watch.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static char folder[256];

static void path_in_folder(char *out, size_t size, const char *name)
{
	snprintf(out, size, "%s/%s", folder, name);
}

/* the whole of a file in a buffer (empty if there is none) */
static size_t read_file(const char *path, char *buffer, size_t capacity)
{
	size_t size = 0;
	int file = open(path, O_RDONLY);

	buffer[0] = 0;
	if (file < 0)
		return 0;
	for (;;)
	{
		ssize_t got = read(file, buffer + size, capacity - 1 - size);

		if (got <= 0)
			break;
		size += (size_t)got;
	}
	close(file);
	buffer[size] = 0;
	return size;
}

static unsigned count_lines(const char *text)
{
	unsigned lines = 0;

	for (; *text; text++)
		if (*text == '\n')
			lines++;
	return lines;
}

/* ---------- the watch */

static void test_watch_stalls(void)
{
	wiiu_shell_watch watch;
	uint32_t stalled = 0;
	uint32_t now = 1000;
	unsigned told = 0;
	uint32_t last_told = 0;

	wiiu_shell_watch_init(&watch, now);
	/* no stall before a first frame: a game that is loading has drawn none */
	CHECK(wiiu_shell_watch_poll(&watch, now + 60000, &stalled) == WIIU_WATCH_QUIET);

	wiiu_shell_watch_frame(&watch);
	CHECK(wiiu_shell_watch_poll(&watch, now + 100, &stalled) == WIIU_WATCH_QUIET); /* sees the frame */
	CHECK(wiiu_shell_watch_poll(&watch, now + 2900, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, now + 100 + 3000, &stalled) == WIIU_WATCH_STALLED && stalled == 3000);
	/* told again 5 s later, then every 5 s, twelve times in all */
	CHECK(wiiu_shell_watch_poll(&watch, now + 100 + 7999, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, now + 100 + 8000, &stalled) == WIIU_WATCH_STALLED && stalled == 8000);
	told = 2;
	for (now = 1000 + 100 + 13000; now < 1000 + 100 + 200000; now += 1000)
	{
		if (wiiu_shell_watch_poll(&watch, now, &stalled) == WIIU_WATCH_STALLED)
		{
			told++;
			last_told = stalled;
		}
	}
	CHECK(told == WIIU_WATCH_STALL_TELLINGS);
	CHECK(last_told == 3000 + 5000 * (WIIU_WATCH_STALL_TELLINGS - 1));

	/* a frame ends the stall, and the count starts again */
	wiiu_shell_watch_frame(&watch);
	now += 1000;
	CHECK(wiiu_shell_watch_poll(&watch, now, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, now + 3000, &stalled) == WIIU_WATCH_STALLED && stalled == 3000);
}

static void test_watch_background_and_close(void)
{
	wiiu_shell_watch watch;
	uint32_t stalled = 0;

	wiiu_shell_watch_init(&watch, 0);
	wiiu_shell_watch_frame(&watch);
	CHECK(wiiu_shell_watch_poll(&watch, 100, &stalled) == WIIU_WATCH_QUIET);

	/* in the background (HOME) a game draws nothing: that is no stall, and coming back starts the count again */
	wiiu_shell_watch_background(&watch, 1, 500);
	CHECK(wiiu_shell_watch_poll(&watch, 60000, &stalled) == WIIU_WATCH_QUIET);
	wiiu_shell_watch_background(&watch, 0, 60000);
	CHECK(wiiu_shell_watch_poll(&watch, 62999, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, 63000, &stalled) == WIIU_WATCH_STALLED && stalled == 3000);

	/* the close deadline: the earliest stands, and it ends the process whatever the frames do */
	wiiu_shell_watch_arm_exit(&watch, 70000, 10000);
	wiiu_shell_watch_arm_exit(&watch, 72000, 10000); /* later: no effect */
	wiiu_shell_watch_frame(&watch);
	CHECK(wiiu_shell_watch_poll(&watch, 79999, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, 80000, &stalled) == WIIU_WATCH_END);
	CHECK(wiiu_shell_watch_poll(&watch, 90000, &stalled) == WIIU_WATCH_END);
	wiiu_shell_watch_arm_exit(&watch, 70000, 1000); /* sooner: it stands */
	CHECK(wiiu_shell_watch_poll(&watch, 71000, &stalled) == WIIU_WATCH_END);
}

static void test_watch_wraps_with_the_clock(void)
{
	/* milliseconds wrap after 49 days: differences are what count */
	wiiu_shell_watch watch;
	uint32_t stalled = 0;
	const uint32_t near_the_end = 0xFFFFFF00u;

	wiiu_shell_watch_init(&watch, near_the_end);
	wiiu_shell_watch_frame(&watch);
	CHECK(wiiu_shell_watch_poll(&watch, near_the_end, &stalled) == WIIU_WATCH_QUIET);
	CHECK(wiiu_shell_watch_poll(&watch, near_the_end + 3000, &stalled) == WIIU_WATCH_STALLED && stalled == 3000);
	wiiu_shell_watch_arm_exit(&watch, near_the_end + 3000, 10000);
	CHECK(wiiu_shell_watch_poll(&watch, near_the_end + 12999, &stalled) != WIIU_WATCH_END);
	CHECK(wiiu_shell_watch_poll(&watch, near_the_end + 13000, &stalled) == WIIU_WATCH_END);
}

static void test_watch_describes(void)
{
	wiiu_shell_watch watch;
	char text[200];
	size_t length;

	wiiu_shell_watch_init(&watch, 0);
	length = wiiu_shell_watch_describe(&watch, 1000, 0, 0, text, sizeof(text));
	CHECK(length == strlen(text) && strcmp(text, "frame 0; in the foreground") == 0);
	wiiu_shell_watch_frame(&watch);
	wiiu_shell_watch_frame(&watch);
	wiiu_shell_watch_phase(&watch, "rendering", 4000);
	wiiu_shell_watch_background(&watch, 1, 4100);
	wiiu_shell_watch_describe(&watch, 5250, 1, 1000, text, sizeof(text));
	CHECK(strcmp(text, "frame 2 (1 in the last second), its rendering phase ended 1.25 s ago; in the background") == 0);
	wiiu_shell_watch_describe(&watch, 5250, 1, 5000, text, sizeof(text));
	CHECK(strstr(text, "(1 in the last 5 s)") != NULL);
	/* never beyond the buffer */
	{
		char small[16];

		length = wiiu_shell_watch_describe(&watch, 5250, 1, 5000, small, sizeof(small));
		CHECK(length == strlen(small) && length == sizeof(small) - 1);
		CHECK(wiiu_shell_watch_describe(&watch, 0, 0, 0, small, 0) == 0);
	}
}

/* ---------- the log */

static uint32_t fake_now(void)
{
	return 12345;
}

static unsigned echoed;

static void echo(const char *line)
{
	(void)line;
	echoed++;
}

static wiiu_shell_log log_instance;

static void test_log(void)
{
	wiiu_shell_log_config config;
	char path[300];
	static char text[WIIU_LOG_SLOTS * WIIU_LOG_LINE_BYTES * 2];
	uint32_t pending, queued, written, dropped;
	char long_line[1000];
	int i;

	path_in_folder(path, sizeof(path), "log.txt");
	unlink(path);
	config.path = path;
	config.now_ms = fake_now;
	config.thread_setup = NULL;
	config.echo = echo;
	CHECK(wiiu_shell_log_start(&log_instance, &config) == 0);

	wiiu_shell_log_write(&log_instance, "hello %d", 42);
	wiiu_shell_log_text(&log_instance, "two\nlines\r");
	memset(long_line, 'x', sizeof(long_line) - 1);
	long_line[sizeof(long_line) - 1] = 0;
	wiiu_shell_log_text(&log_instance, long_line);
	CHECK(wiiu_shell_log_flush(&log_instance, 5000));
	read_file(path, text, sizeof(text));
	CHECK(strncmp(text, "12.345 hello 42\n12.345 two lines \n12.345 xxxx", 44) == 0);
	CHECK(count_lines(text) == 3);
	/* a long line is cut to a slot, and still ends with its newline */
	{
		const char *third = strchr(strchr(text, '\n') + 1, '\n') + 1;

		CHECK(strlen(third) == WIIU_LOG_LINE_BYTES && third[strlen(third) - 1] == '\n');
	}
	CHECK(echoed == 3);
	wiiu_shell_log_counts(&log_instance, &pending, &queued, &written, &dropped);
	CHECK(pending == 0 && queued == 3 && written == 3 && dropped == 0);

	/* a burst bigger than the queue: the lines beyond it are dropped, counted and said so, and nothing is lost that fitted */
	for (i = 0; i < WIIU_LOG_SLOTS + 100; i++)
		wiiu_shell_log_write(&log_instance, "burst %d", i);
	wiiu_shell_log_counts(&log_instance, &pending, &queued, &written, &dropped);
	/* (the worker may have taken some while the burst went in: what was not accepted was dropped) */
	CHECK(queued + dropped == 3 + WIIU_LOG_SLOTS + 100);
	CHECK(wiiu_shell_log_flush(&log_instance, 10000));
	wiiu_shell_log_counts(&log_instance, &pending, &queued, &written, &dropped);
	CHECK(pending == 0 && written == queued);
	read_file(path, text, sizeof(text));
	CHECK(count_lines(text) >= written + (dropped ? 1 : 0));
	if (dropped)
		CHECK(strstr(text, " log lines were dropped: the queue was full") != NULL);
	/* the lines that were accepted are in order */
	CHECK(strstr(text, "burst 0\n") != NULL);
}

static void test_log_card_trouble(void)
{
	static wiiu_shell_log broken;
	wiiu_shell_log_config config;
	uint32_t pending, queued, written, dropped;

	/* a card that is not there: the lines are lost, counted, and nothing waits */
	config.path = "/nonexistent-folder/log.txt";
	config.now_ms = fake_now;
	config.thread_setup = NULL;
	config.echo = NULL;
	CHECK(wiiu_shell_log_start(&broken, &config) == 0);
	wiiu_shell_log_write(&broken, "nowhere to go");
	CHECK(wiiu_shell_log_flush(&broken, 5000));
	wiiu_shell_log_counts(&broken, &pending, &queued, &written, &dropped);
	CHECK(queued == 1 && written == 0 && broken.lost == 1 && broken.write_errors == 1);
}

/* ---------- the black box */

static size_t fake_where(void *thread, char *out, size_t capacity)
{
	if (!thread)
		return 0; /* a thread that is gone says nothing */
	return (size_t)snprintf(out, capacity, "R pc=%08x lr=%08x", (unsigned)(uintptr_t)thread, (unsigned)(uintptr_t)thread + 4);
}

static void test_blackbox(void)
{
	static wiiu_shell_blackbox box;
	static char text[WIIU_BLACKBOX_SLOTS * WIIU_BLACKBOX_RECORD_BYTES + 64];
	char path[300], previous[300];
	wiiu_blackbox_facts facts;
	char record[WIIU_BLACKBOX_RECORD_BYTES];
	size_t length;
	int i;

	path_in_folder(path, sizeof(path), "blackbox.txt");
	path_in_folder(previous, sizeof(previous), "blackbox-previous.txt");
	unlink(path);
	unlink(previous);
	wiiu_shell_blackbox_init(&box, path, fake_where);
	CHECK(wiiu_shell_blackbox_tag(&box, "game", (void *)0x1000) == 0);
	CHECK(wiiu_shell_blackbox_tag(&box, "gone", NULL) == 0);
	CHECK(wiiu_shell_blackbox_tag(&box, "log", (void *)0x2000) == 0);

	memset(&facts, 0, sizeof(facts));
	facts.now_ms = 61250;
	facts.frames = 1800;
	facts.phase = "rendering";
	facts.phase_ago_ms = 12;
	facts.log_pending = 3;
	facts.log_written = 400;
	facts.log_dropped = 2;
	facts.memory = "12 MB in use";
	length = wiiu_shell_blackbox_format(&box, &facts, record, sizeof(record));
	CHECK(length == strlen(record));
	CHECK(strstr(record, "== 61.250 s: frame 1800, its rendering phase ended 12 ms ago; in the foreground; "
		"log: 3 waiting, 400 written, 2 dropped; 12 MB in use\n") == record);
	CHECK(strstr(record, "  game R pc=00001000 lr=00001004\n") != NULL && strstr(record, "  log R pc=00002000") != NULL);
	CHECK(strstr(record, "gone") == NULL);

	/* untagged threads leave the record; a record that does not fit is cut, not overrun */
	wiiu_shell_blackbox_untag(&box, (void *)0x1000);
	length = wiiu_shell_blackbox_format(&box, &facts, record, sizeof(record));
	CHECK(strstr(record, "game") == NULL && strstr(record, "log R") != NULL);
	{
		char small[40];

		length = wiiu_shell_blackbox_format(&box, &facts, small, sizeof(small));
		CHECK(length == strlen(small) && length < sizeof(small));
	}

	/* the file holds the last three records, whatever the number made */
	for (i = 0; i < 5; i++)
	{
		facts.frames = (uint32_t)(100 * (i + 1));
		CHECK(wiiu_shell_blackbox_tick(&box, &facts) == 0);
	}
	read_file(path, text, sizeof(text));
	CHECK(strstr(text, "frame 100,") == NULL && strstr(text, "frame 200,") == NULL);
	CHECK(strstr(text, "frame 300,") != NULL && strstr(text, "frame 400,") != NULL && strstr(text, "frame 500,") != NULL);
	CHECK(strstr(text, "frame 300,") < strstr(text, "frame 400,") && strstr(text, "frame 400,") < strstr(text, "frame 500,"));
	{
		unsigned records = 0;
		const char *at = text;

		while ((at = strstr(at, "== ")) != NULL)
		{
			records++;
			at += 3;
		}
		CHECK(records == WIIU_BLACKBOX_SLOTS);
	}

	/* a crash leaves the file; the next run takes it aside, and a clean stop leaves none */
	CHECK(wiiu_shell_blackbox_take_previous(path, previous) > 0);
	CHECK(access(path, F_OK) != 0);
	read_file(previous, text, sizeof(text));
	CHECK(strstr(text, "frame 500,") != NULL);
	CHECK(wiiu_shell_blackbox_take_previous(path, previous) == 0);
	CHECK(wiiu_shell_blackbox_tick(&box, &facts) == 0 && access(path, F_OK) == 0);
	wiiu_shell_blackbox_stop(&box);
	CHECK(access(path, F_OK) != 0);

	/* a card that is not there: the error is the caller's to log */
	wiiu_shell_blackbox_init(&box, "/nonexistent-folder/blackbox.txt", fake_where);
	CHECK(wiiu_shell_blackbox_tick(&box, &facts) != 0);
	/* the thread list is full at its limit */
	for (i = 0; i < WIIU_BLACKBOX_THREADS; i++)
		CHECK(wiiu_shell_blackbox_tag(&box, "t", (void *)(uintptr_t)(0x100 + i)) == 0);
	CHECK(wiiu_shell_blackbox_tag(&box, "one too many", (void *)0x9999) == -1);
}

int main(void)
{
	char template_[] = "/tmp/wiiu-shell-test-XXXXXX";

	if (!mkdtemp(template_))
	{
		printf("no temporary folder\n");
		return 1;
	}
	snprintf(folder, sizeof(folder), "%s", template_);

	test_watch_stalls();
	test_watch_background_and_close();
	test_watch_wraps_with_the_clock();
	test_watch_describes();
	test_log();
	test_log_card_trouble();
	test_blackbox();

	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("shell tests passed\n");
	return 0;
}
