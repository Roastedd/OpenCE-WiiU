/*
WIIU_POSIX_NET.C

posix_net.c for the Wii U, as far as it is written: no network. Every socket call fails with Winsock's "network is down" (10050), so that the
game runs offline (campaign, split screen) and the network code finds no way out; the process, the secret and the desktop's links have no meaning on
a console. The sockets are nsysnet's (wut's newlib has them); system link comes first when they are written (WIIU_PORT_PLAN.md, 6.5).
*/

#include "posix.h"

#include <stddef.h>

int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length, size_t *output_length);

#define WSAENETDOWN 10050

int posix_socket_last_error(void)
{
	return WSAENETDOWN;
}

int posix_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	return -1;
}

int posix_socket_close(int socket)
{
	(void)socket;
	return -1;
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_listen(int socket, int backlog)
{
	(void)socket;
	(void)backlog;
	return -1;
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address, int address_length)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_shutdown(int socket, int how)
{
	(void)socket;
	(void)how;
	return -1;
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	(void)socket;
	(void)nonblocking;
	return -1;
}

int posix_socket_bytes_available(int socket, posix_ulong *count)
{
	(void)socket;
	*count = 0;
	return -1;
}

int posix_socket_set_nodelay(int socket)
{
	(void)socket;
	return -1;
}

int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)length;
	return -1;
}

int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)length;
	return -1;
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_getpeername(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count, posix_long timeout_seconds,
	posix_long timeout_microseconds, int infinite)
{
	(void)read;
	(void)write;
	(void)error;
	(void)timeout_seconds;
	(void)timeout_microseconds;
	(void)infinite;
	/* nothing is ready */
	if (read_count)
		*read_count = 0;
	if (write_count)
		*write_count = 0;
	if (error_count)
		*error_count = 0;
	return 0;
}

posix_ulong posix_local_ipv4_address(void)
{
	return 0;
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	(void)host;
	return 0;
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

int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
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
