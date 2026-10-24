/* (nsysnet has no interface list: miniupnpc asks the names of interfaces for the scope of an IPv6 address, which its IPv4 search never reads; wiiu_upnp.c answers) */
#ifndef WIIU_NET_IF_H
#define WIIU_NET_IF_H
#define IF_NAMESIZE 16
unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
#endif
