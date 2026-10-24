/* What miniupnpc (port/third_party/miniupnpc) takes from the BSD socket headers and wut's (nsysnet's) do not have: IPv6 (never asked for: internet play's
UPnP searches IPv4) and the receive timeout (setsockopt then fails, which it ignores). Included with -include for those sources only. */
#ifndef WIIU_NET_COMPAT_H
#define WIIU_NET_COMPAT_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#ifndef AF_INET6
#define AF_INET6 10
#define PF_INET6 AF_INET6
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#define SO_SNDTIMEO 0x1005
#endif
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

struct in6_addr
{
	unsigned char s6_addr[16];
};

struct sockaddr_in6
{
	sa_family_t sin6_family;
	in_port_t sin6_port;
	unsigned int sin6_flowinfo;
	struct in6_addr sin6_addr;
	unsigned int sin6_scope_id;
};
#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF 9
#define IPV6_MULTICAST_HOPS 10
#endif
#define IFNAMSIZ 16
#define SIOCGIFADDR 0x8915

/* (what minissdpc.c's search names a local interface by: asked only when internet play gives it one, which it does not; the search then fails there) */
struct ip_mreqn
{
	struct in_addr imr_multiaddr;
	struct in_addr imr_address;
	int imr_ifindex;
};

struct ifreq
{
	char ifr_name[IFNAMSIZ];
	struct sockaddr_in ifr_addr;
};

/* (the wildcard IPv6 address, which minissdpc.c's multicast search binds to when it is asked for IPv6; wiiu_upnp.c has it) */
extern const struct in6_addr in6addr_any;
#endif
