#include "card.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void card_prepare(void)
{
	/* each level; failing because it is there is the usual case */
	mkdir("/vol/external01/wiiu", 0755);
	mkdir("/vol/external01/wiiu/apps", 0755);
	mkdir(PROBE_DIRECTORY, 0755);
}

static void path_of(const char *name, char *path, size_t size)
{
	size_t length = 0;

	while (PROBE_DIRECTORY[length])
	{
		path[length] = PROBE_DIRECTORY[length];
		length++;
	}
	path[length++] = '/';
	while (*name && length < size - 1)
		path[length++] = *name++;
	path[length] = 0;
}

int card_read(const char *name, char *buffer, size_t capacity)
{
	char path[160];
	size_t size = 0;
	int file;

	path_of(name, path, sizeof(path));
	file = open(path, O_RDONLY);
	if (file < 0)
		return -1;
	while (size < capacity - 1)
	{
		ssize_t got = read(file, buffer + size, capacity - 1 - size);

		if (got <= 0)
			break;
		size += (size_t)got;
	}
	close(file);
	buffer[size] = 0;
	return (int)size;
}

void card_remove(const char *name)
{
	char path[160];

	path_of(name, path, sizeof(path));
	unlink(path);
}

int card_write(const char *name, const char *data, size_t size, int append)
{
	char path[160];
	size_t done = 0;
	int error = 0;
	int file;

	path_of(name, path, sizeof(path));

	file = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
	if (file < 0)
		return errno ? errno : EIO;
	while (done < size)
	{
		ssize_t written = write(file, data + done, size - done);

		if (written <= 0)
		{
			error = errno ? errno : EIO;
			break;
		}
		done += (size_t)written;
	}
	if (!error && fsync(file) != 0)
		error = errno ? errno : EIO;
	if (close(file) != 0 && !error)
		error = errno ? errno : EIO;
	return error;
}
