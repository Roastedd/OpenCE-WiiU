/* (no Unix sockets on the Wii U: miniupnpc asks minissdpd over one, and finds none) */
#ifndef WIIU_SYS_UN_H
#define WIIU_SYS_UN_H
#include <sys/socket.h>
struct sockaddr_un
{
	sa_family_t sun_family;
	char sun_path[108];
};
#endif
