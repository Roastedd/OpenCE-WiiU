/*
PLATFORM.H (the PC peer's stand-in)

port/linux/src/platform.h reads the Xbox SDK's headers, which only compile for the game's 32-bit ABI. The PC peer (tools/pcpeer) compiles p2p_signal.c, the real
MQTT code of internet play, unchanged on a desktop of any width, and this header is all of the platform layer it needs: the log, and the BSD socket types and
Winsock error names p2p_signal.c uses.
*/

#ifndef __HALO_PCPEER_PLATFORM_H
#define __HALO_PCPEER_PLATFORM_H

#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
