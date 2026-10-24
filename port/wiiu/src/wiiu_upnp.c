/*
WIIU_UPNP.C

What miniupnpc (port/third_party/miniupnpc, built for the Wii U with wiiu_net_compat.h standing in for the BSD socket headers wut's nsysnet lacks) takes
from what the Wii U does not have: the names of network interfaces (an IPv6 link-local address's scope, which an IPv4 search never reads) and the IPv6
wildcard address. minissdpd's Unix socket is asked for and not there (the socket call fails), and the search goes to the network.
*/

#include "../include/wiiu_net_compat.h"

#include <stddef.h>

const struct in6_addr in6addr_any = { { 0 } };
unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);

unsigned int if_nametoindex(const char *name)
{
	(void)name;
	return 0;
}

char *if_indextoname(unsigned int index, char *name)
{
	(void)index;
	(void)name;
	return NULL;
}
