/*
MODSTORE_CHECK.C (test)

The mod store's own code (port/linux/src/modstore.c, copied unchanged next to this file by tools/test_modstore.py and
included whole, so that its statics can be reached) run on this machine against a local web server: the real
update_https_request (posix_update.c, plain HTTP on 127.0.0.1), the real JSON reader, SHA-256 and Ed25519
(server_json.c, p2p_crypto.c, Monocypher), TOML (tomlc17.c) and the files (posix_files.c).

    modstore_check ROOT URL KEY COMMAND...

ROOT is the data root (the mods folder is ROOT/mods); URL and KEY are mods.store_url and mods.store_key. The commands run in
order, each printing a line the test reads:

    open                   modstore_open, wait for the catalog:  open phase=3 count=2 message=...
    refresh                the same, fetching the catalog again
    list                   one line a mod:  mod <index> <id> state=<n> installed=<version> size=<bytes>
    install <id>           start the install and wait for it:  install <id> started=1 message=... changed=1
    remove <id>            likewise
    stop-after <id> <n>    start the install, cancel it once n bytes are done, wait:  stop <id> done=<bytes> message=...
    own <id>               make the player's own folder of that name in the mods folder (a mod.toml, no .store): own <id>
    close                  modstore_close
    path <path>            path_ok:  path <path> 0|1          (the paths read from the rest of the line)
    hash <file>            the file's SHA-256 by sha256_add in pieces of 7 bytes:  hash <hex>
    url <manifest> <path>  url_file:  url <address>
*/

#define _GNU_SOURCE
#include "modstore.c"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static const char *check_root, *check_url, *check_key;

void platform_log(const char *format, ...)
{
	va_list arguments;

	fputs("log: ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

const char *platform_data_root(void)
{
	return check_root;
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "mods.store_url"))
		return check_url;
	if (!strcmp(name, "mods.store_key"))
		return check_key;
	return "";
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	memset(buffer, 0x5A, size);
}

/* ---------- the files layer, as posix_files.c has it (which names a Linux stat's fields: the test also runs on a Mac) */

int posix_stat(const char *path, struct posix_file_information *information)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -1;
	memset(information, 0, sizeof(*information));
	information->flags = S_ISDIR(st.st_mode) ? _posix_file_is_directory : 0;
	information->size_low = (posix_ulong)((unsigned long long)st.st_size & 0xFFFFFFFFu);
	information->size_high = (posix_ulong)((unsigned long long)st.st_size >> 32);
	return 0;
}

int posix_make_directory(const char *path)
{
	return mkdir(path, 0755);
}

void *posix_directory_open(const char *path)
{
	return opendir(path);
}

int posix_directory_next(void *directory, char *name, posix_ulong name_size)
{
	struct dirent *entry;

	while ((entry = readdir((DIR *)directory)) != NULL)
	{
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		snprintf(name, name_size, "%s", entry->d_name);
		return 1;
	}
	return 0;
}

void posix_directory_close(void *directory)
{
	closedir((DIR *)directory);
}

/* the free space the test says there is (the environment's MODSTORE_CHECK_FREE, in bytes, else the disk's) */
int posix_disk_space(const char *path, posix_ulong *free_low, posix_ulong *free_high, posix_ulong *total_low, posix_ulong *total_high)
{
	struct statvfs st;
	unsigned long long free_bytes;
	const char *said = getenv("MODSTORE_CHECK_FREE");

	if (statvfs(path, &st) != 0)
		return -1;
	free_bytes = said ? strtoull(said, NULL, 10) : (unsigned long long)st.f_bavail * st.f_frsize;
	*free_low = (posix_ulong)(free_bytes & 0xFFFFFFFFu);
	*free_high = (posix_ulong)(free_bytes >> 32);
	*total_low = *total_high = 0;
	return 0;
}

/* (p2p.c's, which p2p_crypto.c's WebRTC credentials use) */
void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	hex_write(bytes, size, text);
}

static void pause_a_little(void)
{
	struct timespec wait = { 0, 5 * 1000 * 1000 };

	nanosleep(&wait, NULL);
}

/* waits (at most 60 s) until no job runs */
static int wait_idle(void)
{
	int tries;

	for (tries = 0; tries < 12000; tries++)
	{
		int running;

		pthread_mutex_lock(&store.lock);
		running = store.running;
		pthread_mutex_unlock(&store.lock);
		if (!running)
			return 1;
		pause_a_little();
	}
	return 0;
}

static long find(const char *id)
{
	long index;
	char text[64];

	for (index = 0; index < modstore_count(); index++)
	{
		modstore_field(index, _modstore_field_id, text, sizeof(text));
		if (!strcmp(text, id))
			return index;
	}
	return -1;
}

int main(int argc, char **argv)
{
	int at;

	if (argc < 5)
		return 2;
	check_root = argv[1];
	check_url = argv[2];
	check_key = argv[3];
	for (at = 4; at < argc; at++)
	{
		const char *command = argv[at];
		char message[256];

		if (!strcmp(command, "open") || !strcmp(command, "refresh"))
		{
			if (!strcmp(command, "open"))
				modstore_open();
			else
				modstore_refresh();
			wait_idle();
			modstore_message(message, sizeof(message));
			printf("open phase=%d count=%ld configured=%d message=%s\n", modstore_phase(), modstore_count(), modstore_configured(),
				message);
		}
		else if (!strcmp(command, "list"))
		{
			long index;

			for (index = 0; index < modstore_count(); index++)
			{
				char id[64], installed[32];

				modstore_field(index, _modstore_field_id, id, sizeof(id));
				modstore_field(index, _modstore_field_installed, installed, sizeof(installed));
				printf("mod %ld %s state=%d installed=%s size=%lu\n", index, id, modstore_state(index), installed,
					modstore_size(index));
			}
		}
		else if ((!strcmp(command, "install") || !strcmp(command, "remove")) && at + 1 < argc)
		{
			long index = find(argv[++at]);
			int started = !strcmp(command, "install") ? modstore_install(index) : modstore_remove(index);

			wait_idle();
			modstore_message(message, sizeof(message));
			printf("%s %s started=%d changed=%d message=%s\n", command, argv[at], started, modstore_changed(), message);
		}
		else if (!strcmp(command, "stop-after") && at + 2 < argc)
		{
			long index = find(argv[++at]);
			unsigned long limit = strtoul(argv[++at], NULL, 10), done, total;
			int started = modstore_install(index), tries;

			for (tries = 0; tries < 12000 && started; tries++)
			{
				modstore_progress(&done, &total);
				if (done >= limit || !modstore_job())
					break;
				pause_a_little();
			}
			modstore_cancel();
			wait_idle();
			modstore_progress(&done, &total);
			modstore_message(message, sizeof(message));
			printf("stop %s started=%d done=%lu changed=%d message=%s\n", argv[at - 1], started, done, modstore_changed(), message);
		}
		else if (!strcmp(command, "own") && at + 1 < argc)
		{
			char folder[MAXIMUM_PATH], file[MAXIMUM_PATH];

			make_folders(platform_data_root(), "mods", 1);
			mod_folder(argv[++at], folder, sizeof(folder));
			posix_make_directory(folder);
			snprintf(file, sizeof(file), "%s/mod.toml", folder);
			file_put(file, "name = \"mine\"\n", strlen("name = \"mine\"\n"));
			printf("own %s\n", argv[at]);
		}
		else if (!strcmp(command, "close"))
		{
			modstore_close();
			wait_idle();
			printf("close ok\n");
		}
		else if (!strcmp(command, "path") && at + 1 < argc)
		{
			printf("path %s %d\n", argv[at + 1], path_ok(argv[at + 1]));
			at++;
		}
		else if (!strcmp(command, "hash") && at + 1 < argc)
		{
			struct sha256 context;
			unsigned char digest[HASH_BYTES], piece[7];
			char hex[HASH_BYTES * 2 + 1];
			FILE *file = fopen(argv[++at], "rb");
			size_t got;

			if (!file)
				return 3;
			sha256_begin(&context);
			while ((got = fread(piece, 1, sizeof(piece), file)) > 0)
				sha256_add(&context, piece, got);
			fclose(file);
			sha256_end(&context, digest);
			hex_write(digest, HASH_BYTES, hex);
			printf("hash %s\n", hex);
		}
		else if (!strcmp(command, "url") && at + 2 < argc)
		{
			char url[MAXIMUM_URL];

			if (url_file(argv[at + 1], argv[at + 2], url, sizeof(url)))
				printf("url %s\n", url);
			else
				printf("url (none)\n");
			at += 2;
		}
		else
		{
			fprintf(stderr, "unknown command %s\n", command);
			return 2;
		}
		fflush(stdout);
	}
	return 0;
}
