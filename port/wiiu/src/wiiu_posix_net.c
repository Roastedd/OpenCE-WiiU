/*
WIIU_POSIX_NET.C

posix_net.c for the Wii U: the sockets of Cafe OS's nsysnet, which wut gives as BSD sockets (<sys/socket.h>, <poll.h>). The game's Winsock layer (xnet.c)
sits on posix.h's calls, as it does on the other ports; this makes them, so that system link and internet play have a network to use.

Where nsysnet differs from the Linux calls: SOL_SOCKET is -1 (Winsock's is 0xffff); a socket is made non-blocking by the option SO_NONBLOCK; there is no
accept4, recvmsg, MSG_NOSIGNAL or getifaddrs (a datagram larger than the buffer is not told of, and the address the default route leaves from is found by
"connecting" a datagram socket); its sockaddr_in is Winsock's (a 16-bit family first). While the app is in the background, closing, or told by the system
to stop using the network (wiiu_shell.c), every call but close fails as Winsock's "network is down", and select waits out its time without a socket ready.
*/

#include "posix.h"

#include "port_config.h"
#include "wiiu_shell.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <coreinit/thread.h>
#include <coreinit/time.h>

int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length, size_t *output_length);

/* Winsock error codes (winsockx.h) */
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAEOPNOTSUPP 10045
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAEHOSTDOWN 10064
#define WSAEHOSTUNREACH 10065

/* Winsock SOL_SOCKET option values (winsockx.h) */
#define WINSOCK_SOL_SOCKET 0xffff
#define WINSOCK_SO_REUSEADDR 0x0004
#define WINSOCK_SO_KEEPALIVE 0x0008
#define WINSOCK_SO_BROADCAST 0x0020
#define WINSOCK_SO_LINGER 0x0080
#define WINSOCK_SO_SNDBUF 0x1001
#define WINSOCK_SO_RCVBUF 0x1002
#define WINSOCK_SO_ERROR 0x1007
#define WINSOCK_SO_TYPE 0x1008

/* Like Winsock's last error, this belongs to the calling thread. */
static __thread int last_error;

/* the first calls of each kind, in the log: what the game asks of the network, and what nsysnet answers (the sockets have not run on a console before) */
static void trace(const char *what, int socket, const void *address, int result)
{
	static unsigned int told;
	const unsigned char *bytes = (const unsigned char *)address;

	if (told >= 120u)
		return;
	told++;
	if (address)
		wiiu_log("Net: %s socket %d, %u.%u.%u.%u:%u -> %d (errno %d)", what, socket, bytes[4], bytes[5], bytes[6], bytes[7], (bytes[2] << 8) | bytes[3], result,
			result < 0 ? errno : 0);
	else
		wiiu_log("Net: %s socket %d -> %d (errno %d)", what, socket, result, result < 0 ? errno : 0);
}

static int fail(void)
{
	switch (errno)
	{
	case EINTR: last_error = WSAEINTR; break;
	case EBADF: last_error = WSAEBADF; break;
	case EACCES: case EPERM: last_error = WSAEACCES; break;
	case EFAULT: last_error = WSAEFAULT; break;
	case EMFILE: case ENFILE: last_error = WSAEMFILE; break;
	case EAGAIN: last_error = WSAEWOULDBLOCK; break;
	case EINPROGRESS: last_error = WSAEINPROGRESS; break;
	case EALREADY: last_error = WSAEALREADY; break;
	case ENOTSOCK: last_error = WSAENOTSOCK; break;
	case EDESTADDRREQ: last_error = WSAEDESTADDRREQ; break;
	case EMSGSIZE: last_error = WSAEMSGSIZE; break;
	case EPROTOTYPE: last_error = WSAEPROTOTYPE; break;
	case ENOPROTOOPT: last_error = WSAENOPROTOOPT; break;
	case EPROTONOSUPPORT: last_error = WSAEPROTONOSUPPORT; break;
	case EOPNOTSUPP: last_error = WSAEOPNOTSUPP; break;
	case EAFNOSUPPORT: last_error = WSAEAFNOSUPPORT; break;
	case EADDRINUSE: last_error = WSAEADDRINUSE; break;
	case EADDRNOTAVAIL: last_error = WSAEADDRNOTAVAIL; break;
	case ENETDOWN: last_error = WSAENETDOWN; break;
	case ENETUNREACH: last_error = WSAENETUNREACH; break;
	case ENETRESET: last_error = WSAENETRESET; break;
	case ECONNABORTED: last_error = WSAECONNABORTED; break;
	case ECONNRESET: case EPIPE: last_error = WSAECONNRESET; break;
	case ENOBUFS: case ENOMEM: last_error = WSAENOBUFS; break;
	case EISCONN: last_error = WSAEISCONN; break;
	case ENOTCONN: last_error = WSAENOTCONN; break;
	case ETIMEDOUT: last_error = WSAETIMEDOUT; break;
	case ECONNREFUSED: last_error = WSAECONNREFUSED; break;
	case EHOSTUNREACH: last_error = WSAEHOSTUNREACH; break;
	default: last_error = WSAEINVAL; break;
	}
	return -1;
}

static int succeed(int result)
{
	if (result < 0)
		return fail();
	last_error = 0;
	return result;
}

/* network.wiiu_sockets (off until the sockets have run on a console): the network is down, as it was before them */
static int sockets_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = config_boolean("network.wiiu_sockets");
	return enabled;
}

/* A listening socket that nsysnet's poll calls readable, and that has nothing to accept (seen every poll, fifty times a second, while a game was hosted: the
game then logged "accept_endpoint() returned NULL" into debug.txt each time, a write to the card on the game's thread): a connection is waited for, but not
told of again for a short while after an accept that found none, a longer one each time, to 200 ms. */
#define LISTENERS 256
static struct
{
	unsigned char listening;
	unsigned short quiet_ms;
	uint32_t quiet_until;
} listeners[LISTENERS];

/* A datagram socket's receive longer than some length nsysnet refuses with EMSGSIZE, whatever is waiting, where Linux and Windows take any length: internet
play's tunnel reads into 2048 bytes, so every STUN answer and every packet from a peer was lost (the console never learned its public address, and a peer
reached by its pings could not answer them), while the game's 1200 bytes are taken (the console behaviour the warthog-wiiu port's logs found, 2026-10-22). No
datagram on these networks is longer than an Ethernet frame's 1472 bytes of UDP (the tunnel's packets are at most 1431, p2p.c; the game's 1200), so a datagram
socket's receive asks for no more than that, and one still refused is asked again 8 bytes shorter, down to 512: the longest taken is the limit from then on,
logged. Refused at 512 too, it fails as Winsock's WSAEMSGSIZE (a datagram dropped). */
#define DATAGRAM_RECEIVE_MOST 1472
#define DATAGRAM_RECEIVE_LEAST 512
#define DATAGRAM_RECEIVE_STEP 8
static int datagram_receive_limit = DATAGRAM_RECEIVE_MOST;
/* which sockets are datagram sockets (made by posix_socket) */
static unsigned char datagram_sockets[LISTENERS];

static uint32_t clock_ms(void)
{
	return (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
}

/* the network is not the app's to use now: Winsock's "network is down" */
static int unusable(void)
{
	if (sockets_enabled() && wiiu_network_allowed())
		return 0;
	last_error = WSAENETDOWN;
	return 1;
}

int posix_socket_last_error(void)
{
	return last_error;
}

int posix_socket(int family, int type, int protocol)
{
	if (unusable())
		return -1;
	{
		int result = socket(family, type, protocol);

		trace(type == SOCK_STREAM ? "socket (stream)" : "socket (datagram)", result, NULL, result);
		if (result >= 0 && result < LISTENERS)
			datagram_sockets[result] = type == SOCK_DGRAM;
		return succeed(result);
	}
}

/* (a close is always allowed: the sockets are given back when the system asks) */
int posix_socket_close(int socket)
{
	if (socket >= 0 && socket < LISTENERS)
	{
		memset(&listeners[socket], 0, sizeof(listeners[socket]));
		datagram_sockets[socket] = 0;
	}
	return succeed(close(socket));
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	if (unusable())
		return -1;
	{
		int result = bind(socket, address, (socklen_t)address_length);

		trace("bind", socket, address, result);
		return succeed(result);
	}
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	/* a non-blocking connect under way is WSAEWOULDBLOCK in Winsock, which is what the game waits on (connect_endpoint, transport_endpoint_winsock.c) */
	int result;

	if (unusable())
		return -1;
	result = connect(socket, address, (socklen_t)address_length);
	trace("connect", socket, address, result);
	if (result < 0 && (errno == EINPROGRESS || errno == EALREADY))
	{
		last_error = WSAEWOULDBLOCK;
		return -1;
	}
	return succeed(result);
}

int posix_socket_listen(int socket, int backlog)
{
	if (unusable())
		return -1;
	{
		int result = listen(socket, backlog);

		if (result >= 0 && socket >= 0 && socket < LISTENERS)
			memset(&listeners[socket], 0, sizeof(listeners[socket])), listeners[socket].listening = 1;
		trace("listen", socket, NULL, result);
		return succeed(result);
	}
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	socklen_t length = address_length ? (socklen_t)*address_length : 0;
	int result;

	if (unusable())
		return -1;
	/* (with none waiting, nsysnet answers EBUSY where Winsock says it would block) */
	result = accept(socket, address, address_length ? &length : NULL);
	if (address_length)
		*address_length = (int)length;
	if (result >= 0 && result < LISTENERS)
		datagram_sockets[result] = 0;
	if (result >= 0 || errno != EBUSY)
		trace("accept", socket, NULL, result);
	if (socket >= 0 && socket < LISTENERS && listeners[socket].listening)
	{
		if (result >= 0)
		{
			listeners[socket].quiet_ms = 0;
		}
		else if (errno == EBUSY || errno == EAGAIN)
		{
			listeners[socket].quiet_ms = listeners[socket].quiet_ms ? (listeners[socket].quiet_ms * 2 > 200 ? 200 : listeners[socket].quiet_ms * 2) : 10;
			listeners[socket].quiet_until = clock_ms() + listeners[socket].quiet_ms;
		}
	}
	if (result < 0 && errno == EBUSY)
	{
		last_error = WSAEWOULDBLOCK;
		return -1;
	}
	return succeed(result);
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	{
		int result;
		static unsigned int told;

		if (unusable())
			return -1;
		result = (int)send(socket, buffer, (size_t)length, flags);
		if (result < 0 && errno != EAGAIN && errno != EBUSY && told++ < 20u)
			wiiu_log("Net: send socket %d, %d bytes -> %d (errno %d)", socket, length, result, errno);
		else if (result >= 0 && result < length && told++ < 20u)
			wiiu_log("Net: send socket %d, %d bytes -> only %d", socket, length, result);
		return succeed(result);
	}
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address, int address_length)
{
	if (unusable())
		return -1;
	{
		int result = (int)sendto(socket, buffer, (size_t)length, flags, address, (socklen_t)address_length);

		static unsigned int sends;

		if (sends++ < 8u || result < 0)
			trace("sendto", socket, address, result);
		return succeed(result);
	}
}

/* recv, or recvfrom (from_call), with a datagram socket's length kept to what nsysnet takes (datagram_receive_limit); errno is the call's */
static int receive(int socket, void *buffer, int length, int flags, int from_call, void *address, socklen_t *address_length)
{
	static unsigned int told;
	static int told_taken;
	const int datagram = socket >= 0 && socket < LISTENERS && datagram_sockets[socket];
	const socklen_t address_space = address_length ? *address_length : 0;
	const int asked = length;
	int refused = 0;
	int result, error;

	for (;;)
	{
		if (datagram && length > datagram_receive_limit)
			length = datagram_receive_limit;
		result = from_call ? (int)recvfrom(socket, buffer, (size_t)length, flags, address, address_length) : (int)recv(socket, buffer, (size_t)length, flags);
		if (result >= 0 || errno != EMSGSIZE || !datagram || length - DATAGRAM_RECEIVE_STEP < DATAGRAM_RECEIVE_LEAST)
			break;
		/* refused: shorter, this receive and every one after it */
		refused = length;
		length -= DATAGRAM_RECEIVE_STEP;
		if (length < datagram_receive_limit)
			datagram_receive_limit = length;
		if (address_length)
			*address_length = address_space;
	}
	error = errno;
	if (refused && told < 8u)
	{
		told++;
		if (result >= 0 || error != EMSGSIZE)
			wiiu_log("Net: socket %d: a datagram receive of %d bytes refused (EMSGSIZE); %d taken (%s), the most asked of a datagram socket from now on",
				socket, refused, length, result >= 0 ? "a datagram read" : "none waiting");
		else
			wiiu_log("Net: socket %d: a datagram receive refused (EMSGSIZE) at every length from %d down to %d", socket, asked, length);
	}
	else if (datagram && !refused && asked > length && result >= 0 && !told_taken)
	{
		/* (once: that the longest asked is taken with a datagram waiting) */
		told_taken = 1;
		wiiu_log("Net: socket %d: a datagram receive of %d bytes asked for %d, and took a datagram of %d", socket, asked, length, result);
	}
	errno = error;
	return result;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	{
		int result;
		static unsigned int told;

		if (unusable())
			return -1;
		result = receive(socket, buffer, length, flags, 0, NULL, NULL);
		if ((result < 0 && errno != EAGAIN && errno != EBUSY) || result == 0)
		{
			const int error = errno;

			if (told++ < 20u)
				wiiu_log("Net: recv socket %d, %d bytes -> %d (errno %d)", socket, length, result, result < 0 ? error : 0);
			errno = error;
		}
		return succeed(result);
	}
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	socklen_t socket_length = address_length ? (socklen_t)*address_length : 0;
	int result;
	static unsigned int told;

	if (unusable())
		return -1;
	result = receive(socket, buffer, length, flags, 1, address && address_length ? address : NULL, address && address_length ? &socket_length : NULL);
	if (address_length)
		*address_length = (int)socket_length;
	if (result < 0 && errno != EAGAIN && errno != EBUSY && told < 20u)
	{
		const int error = errno;

		told++;
		wiiu_log("Net: recvfrom socket %d, %d bytes -> %d (errno %d)", socket, length, result, error);
		errno = error;
	}
	return succeed(result);
}

int posix_socket_shutdown(int socket, int how)
{
	if (unusable())
		return -1;
	return succeed(shutdown(socket, how));
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	int value = nonblocking ? 1 : 0;

	if (unusable())
		return -1;
	return succeed(setsockopt(socket, SOL_SOCKET, SO_NONBLOCK, &value, sizeof(value)));
}

int posix_socket_set_nodelay(int socket)
{
	int value = 1;

	if (unusable())
		return -1;
	return succeed(setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)));
}

int posix_socket_bytes_available(int socket, posix_ulong *count)
{
	int available = 0;
	int result;

	if (unusable())
		return -1;
	result = ioctl(socket, FIONREAD, &available);
	if (result >= 0)
		*count = (posix_ulong)available;
	return succeed(result);
}

static int translate_option(int level, int name, int *host_level, int *host_name)
{
	if (level != WINSOCK_SOL_SOCKET)
	{
		/* IPPROTO_IP and IPPROTO_TCP levels are the same; the TCP option numbers are nsysnet's own (TCP_NODELAY is set by posix_socket_set_nodelay) */
		*host_level = level;
		*host_name = name;
		return level == IPPROTO_TCP ? -1 : 0;
	}
	*host_level = SOL_SOCKET;
	switch (name)
	{
	case WINSOCK_SO_REUSEADDR: *host_name = SO_REUSEADDR; return 0;
	case WINSOCK_SO_KEEPALIVE: *host_name = SO_KEEPALIVE; return 0;
	case WINSOCK_SO_BROADCAST: *host_name = SO_BROADCAST; return 0;
	case WINSOCK_SO_LINGER: *host_name = SO_LINGER; return 0;
	case WINSOCK_SO_SNDBUF: *host_name = SO_SNDBUF; return 0;
	case WINSOCK_SO_RCVBUF: *host_name = SO_RCVBUF; return 0;
	case WINSOCK_SO_ERROR: *host_name = SO_ERROR; return 0;
	case WINSOCK_SO_TYPE: *host_name = SO_TYPE; return 0;
	default: return -1;
	}
}

int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	int host_level, host_name;

	if (unusable())
		return -1;
	if (translate_option(level, name, &host_level, &host_name) != 0)
	{
		/* Xbox-only options such as SO_ENCRYPT have nothing to do here */
		last_error = 0;
		return 0;
	}
	return succeed(setsockopt(socket, host_level, host_name, value, (socklen_t)length));
}

int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	int host_level, host_name;
	socklen_t socket_length = (socklen_t)*length;
	int result;

	if (unusable())
		return -1;
	if (translate_option(level, name, &host_level, &host_name) != 0)
	{
		last_error = WSAENOPROTOOPT;
		return -1;
	}
	result = getsockopt(socket, host_level, host_name, value, &socket_length);
	*length = (int)socket_length;
	return succeed(result);
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	socklen_t length = (socklen_t)*address_length;
	int result;

	if (unusable())
		return -1;
	result = getsockname(socket, address, &length);
	*address_length = (int)length;
	return succeed(result);
}

int posix_socket_getpeername(int socket, void *address, int *address_length)
{
	socklen_t length = (socklen_t)*address_length;
	int result;

	if (unusable())
		return -1;
	result = getpeername(socket, address, &length);
	*address_length = (int)length;
	return succeed(result);
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count, posix_long timeout_seconds,
	posix_long timeout_microseconds, int infinite)
{
	/* poll, as the Linux layer does it: read for data, the end or an error, write for room or an error, error for urgent data */
	static const int events[3] = { POLLIN, POLLOUT, POLLPRI };
	static const int ready[3] = { POLLIN | POLLHUP | POLLERR, POLLOUT | POLLERR, POLLPRI | POLLERR };
	int *lists[3] = { read, write, error };
	int *counts[3] = { read_count, write_count, error_count };
	struct pollfd descriptors[128];
	long long milliseconds = (long long)timeout_seconds * 1000 + ((long long)timeout_microseconds + 999) / 1000;
	int total = 0;
	int list, index;
	int result;
	int quieted = 0;

	if (!sockets_enabled() || !wiiu_network_allowed())
	{
		/* (nothing is ready; the time is waited out, in short sleeps that notice the network coming back) */
		OSTime until = OSGetSystemTime() + OSMillisecondsToTicks((uint32_t)(infinite || milliseconds > 100 ? 100 : milliseconds < 0 ? 0 : milliseconds));

		while (OSGetSystemTime() < until)
			OSSleepTicks(OSMillisecondsToTicks(5));
		for (list = 0; list < 3; list++)
			if (lists[list] && counts[list])
				*counts[list] = 0;
		return 0;
	}
	for (list = 0; list < 3; list++)
	{
		if (!lists[list] || !counts[list])
			lists[list] = NULL;
		else
			total += *counts[list];
	}
	if (total > (int)(sizeof(descriptors) / sizeof(*descriptors)))
	{
		errno = ENOMEM;
		return fail();
	}
	total = 0;
	for (list = 0; list < 3; list++)
	{
		for (index = 0; lists[list] && index < *counts[list]; index++, total++)
		{
			descriptors[total].fd = lists[list][index];
			descriptors[total].events = events[list];
			descriptors[total].revents = 0;
		}
	}
	result = poll(descriptors, (nfds_t)total, infinite ? -1 : (int)(milliseconds < 0 ? 0 : milliseconds > 0x7fffffff ? 0x7fffffff : milliseconds));
	for (index = 0; index < total && result > 0; index++)
	{
		/* (as select fails on a descriptor that is not open) */
		if (descriptors[index].revents & POLLNVAL)
		{
			errno = EBADF;
			result = -1;
		}
	}
	if (result < 0)
	{
		/* nsysnet fails a whole poll with its own errors when its sockets have been taken (ECANCELED, EBADFD: seen after about 20 minutes in a level with
		internet play's socket open): Winsock's "network is down" or "not a socket", told once a second with a count, and after a short wait: a thread that
		polls again at once spun here, hundreds of calls a second, and filled the log with 58 MB */
		static OSTime last_told;
		static unsigned int failures;
		const int poll_error = errno;
		const OSTime now = OSGetSystemTime();

		failures++;
		if (now - last_told >= OSSecondsToTicks(1))
		{
			wiiu_log("Net: poll failed, errno %d (%u times since the last note)", poll_error, failures);
			last_told = now;
			failures = 0;
		}
		OSSleepTicks(OSMillisecondsToTicks(infinite || milliseconds > 20 ? 20 : milliseconds < 0 ? 0 : (uint32_t)milliseconds));
		errno = poll_error;
		if (errno == ECANCELED)
			errno = ENETDOWN;
		else if (errno == 81 /* EBADFD */)
			errno = EBADF;
		return fail();
	}
	result = 0;
	total = 0;
	for (list = 0; list < 3; list++)
	{
		int kept = 0;

		for (index = 0; lists[list] && index < *counts[list]; index++, total++)
		{
			int descriptor = lists[list][index];
			int pending = 0;
			socklen_t length = sizeof(pending);

			if (!(descriptors[total].revents & ready[list]))
				continue;
			/* (a listening socket that had nothing to accept a moment ago) */
			if (list == 0 && descriptor >= 0 && descriptor < LISTENERS && listeners[descriptor].listening && listeners[descriptor].quiet_ms &&
				(int32_t)(listeners[descriptor].quiet_until - clock_ms()) > 0)
			{
				quieted = 1;
				continue;
			}
			/* Winsock reports a socket writeable once its connect has succeeded; one whose connect failed is in the error set, where POSIX reports
			it writeable with the failure in SO_ERROR. The game takes writeable as connected (connect_endpoint). */
			if (list == 1 && getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &pending, &length) == 0 && pending)
			{
				wiiu_log("Net: socket %d is writeable with the error %d", descriptor, pending);
				errno = pending;
				fail();
				continue;
			}
			lists[list][kept++] = descriptor;
		}
		if (lists[list])
			*counts[list] = kept;
		result += kept;
	}
	/* (everything that was ready was a listening socket kept quiet: a short wait, or the caller's loop would spin on this) */
	if (quieted && result == 0)
		OSSleepTicks(OSMillisecondsToTicks(5));
	/* like Winsock, a select with nothing ready leaves the last error as it was */
	if (result > 0)
		last_error = 0;
	return result;
}

/* the address the default route leaves from: a datagram socket "connected" to an internet address (a documentation one; nothing is sent) has it */
posix_ulong posix_local_ipv4_address(void)
{
	struct sockaddr_in route;
	socklen_t length = sizeof(route);
	posix_ulong result = 0;
	int probe;

	if (!sockets_enabled() || !wiiu_network_allowed())
		return 0;
	probe = socket(AF_INET, SOCK_DGRAM, 0);
	if (probe >= 0)
	{
		memset(&route, 0, sizeof(route));
		route.sin_family = AF_INET;
		route.sin_port = htons(9);
		route.sin_addr.s_addr = htonl(0xC6336401);
		if (connect(probe, (struct sockaddr *)&route, sizeof(route)) == 0 && getsockname(probe, (struct sockaddr *)&route, &length) == 0 &&
			route.sin_addr.s_addr != htonl(INADDR_ANY) && (ntohl(route.sin_addr.s_addr) >> 24) != 127)
		{
			result = route.sin_addr.s_addr;
		}
		close(probe);
	}
	return result;
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	struct addrinfo hints, *results;
	posix_ulong address = 0;

	if (!sockets_enabled() || !wiiu_network_allowed())
		return 0;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, NULL, &hints, &results) != 0)
		return 0;
	if (results && results->ai_addr && results->ai_addr->sa_family == AF_INET)
		address = ((struct sockaddr_in *)results->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(results);
	return address;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	size_t produced = 0;

	mbedtls_platform_entropy_poll(NULL, (unsigned char *)buffer, size, &produced);
}

int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
}

posix_ulong posix_process_id(void)
{
	return 1;
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

/* the identity key of internet play (p2p.c): 32 random bytes made once, in a file on the card */
int posix_user_secret(unsigned char *secret, int size)
{
	static const char path[] = WIIU_DATA_DIRECTORY "/network.key";
	FILE *file = fopen(path, "rb");
	int ok = 0;

	if (file)
	{
		ok = (int)fread(secret, 1, (size_t)size, file) == size;
		fclose(file);
		if (ok)
			return 1;
	}
	posix_random_bytes(secret, (posix_ulong)size);
	file = fopen(path, "wb");
	if (file)
	{
		ok = (int)fwrite(secret, 1, (size_t)size, file) == size;
		fclose(file);
	}
	/* (a card that cannot be written gives a key for this run only) */
	return ok;
}

int posix_discord_connect(void)
{
	return -1;
}

int posix_discord_write(int handle, const void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

int posix_discord_read(int handle, void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

void posix_discord_close(int handle)
{
	(void)handle;
}
