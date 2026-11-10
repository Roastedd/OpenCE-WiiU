/* Force-included when posix_net.c (the game's real socket layer for Linux) is built on macOS: the few Linux calls it makes that macOS does not have. */
#ifndef HALO_PCPEER_COMPAT_H
#define HALO_PCPEER_COMPAT_H

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static inline int accept4(int descriptor, struct sockaddr *address, socklen_t *length, int flags)
{
	(void)flags;
	return accept(descriptor, address, length);
}

static inline ssize_t getrandom(void *buffer, size_t size, unsigned int flags)
{
	extern int getentropy(void *, size_t);
	size_t done = 0;

	(void)flags;
	while (done < size)
	{
		size_t chunk = size - done > 256 ? 256 : size - done;

		if (getentropy((unsigned char *)buffer + done, chunk) != 0)
			return done ? (ssize_t)done : -1;
		done += chunk;
	}
	return (ssize_t)done;
}

#endif
