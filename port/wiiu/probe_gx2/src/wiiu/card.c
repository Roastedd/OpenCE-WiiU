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

int card_write(const char *name, const char *data, size_t size, int append)
{
	char path[160];
	size_t length = 0;
	size_t done = 0;
	int error = 0;
	int file;

	while (PROBE_DIRECTORY[length])
	{
		path[length] = PROBE_DIRECTORY[length];
		length++;
	}
	path[length++] = '/';
	while (*name && length < sizeof(path) - 1)
		path[length++] = *name++;
	path[length] = 0;

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
