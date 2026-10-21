/*
WIIU_UPNP.C

The miniupnpc functions posix_upnp.c calls, answering that there is no router to talk to (the library is not in the Wii U build: it needs BSD socket
headers newlib lacks; internet play over UPnP waits for the Wii U's sockets, WIIU_PORT_PLAN.md 6.5). Their arguments are not read, so they are declared
here with the right count and nothing else.
*/

#include <stddef.h>

void *upnpDiscover(int delay, const char *multicast_interface, const char *minissdpd_socket, int local_port, int ipv6, unsigned char ttl,
	int *error);
void freeUPNPDevlist(void *devices);
void FreeUPNPUrls(void *urls);
int UPNP_GetValidIGD(void *devices, void *urls, void *data, char *lan_address, int lan_address_size, char *wan_address, int wan_address_size);
int UPNP_AddPortMapping(const char *control_url, const char *service_type, const char *external_port, const char *internal_port,
	const char *internal_client, const char *description, const char *protocol, const char *remote_host, const char *lease_duration);
int UPNP_DeletePortMapping(const char *control_url, const char *service_type, const char *external_port, const char *protocol,
	const char *remote_host);
int UPNP_GetExternalIPAddress(const char *control_url, const char *service_type, char *address);
int UPNP_GetGenericPortMappingEntry(const char *control_url, const char *service_type, const char *index, char *external_port, char *internal_client,
	char *internal_port, char *protocol, char *description, char *enabled, char *remote_host, char *lease_duration);
const char *strupnperror(int error);

void *upnpDiscover(int delay, const char *multicast_interface, const char *minissdpd_socket, int local_port, int ipv6, unsigned char ttl,
	int *error)
{
	(void)delay;
	(void)multicast_interface;
	(void)minissdpd_socket;
	(void)local_port;
	(void)ipv6;
	(void)ttl;
	if (error)
		*error = -1;
	return NULL;
}

void freeUPNPDevlist(void *devices)
{
	(void)devices;
}

void FreeUPNPUrls(void *urls)
{
	(void)urls;
}

int UPNP_GetValidIGD(void *devices, void *urls, void *data, char *lan_address, int lan_address_size, char *wan_address, int wan_address_size)
{
	(void)devices;
	(void)urls;
	(void)data;
	(void)lan_address;
	(void)lan_address_size;
	(void)wan_address;
	(void)wan_address_size;
	return 0;
}

int UPNP_AddPortMapping(const char *control_url, const char *service_type, const char *external_port, const char *internal_port,
	const char *internal_client, const char *description, const char *protocol, const char *remote_host, const char *lease_duration)
{
	(void)control_url;
	(void)service_type;
	(void)external_port;
	(void)internal_port;
	(void)internal_client;
	(void)description;
	(void)protocol;
	(void)remote_host;
	(void)lease_duration;
	return -1;
}

int UPNP_DeletePortMapping(const char *control_url, const char *service_type, const char *external_port, const char *protocol,
	const char *remote_host)
{
	(void)control_url;
	(void)service_type;
	(void)external_port;
	(void)protocol;
	(void)remote_host;
	return -1;
}

int UPNP_GetExternalIPAddress(const char *control_url, const char *service_type, char *address)
{
	(void)control_url;
	(void)service_type;
	(void)address;
	return -1;
}

int UPNP_GetGenericPortMappingEntry(const char *control_url, const char *service_type, const char *index, char *external_port, char *internal_client,
	char *internal_port, char *protocol, char *description, char *enabled, char *remote_host, char *lease_duration)
{
	(void)control_url;
	(void)service_type;
	(void)index;
	(void)external_port;
	(void)internal_client;
	(void)internal_port;
	(void)protocol;
	(void)description;
	(void)enabled;
	(void)remote_host;
	(void)lease_duration;
	return -1;
}

const char *strupnperror(int error)
{
	(void)error;
	return "no router";
}
