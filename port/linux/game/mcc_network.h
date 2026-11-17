/* MCC-only extra grenade inventories. The ordinary inventory wire layout
 * and the two native grenade fields are unchanged. */
#ifndef MCC_NETWORK_H
#define MCC_NETWORK_H
#include "cseries.h"
void mcc_network_new_game(void);
void mcc_network_host_tick(void);
void mcc_network_client_tick(void);
word mcc_network_inventory_entry_size(void);
/* port: the grenade entries in the wire order, little-endian, or back (nothing on a little-endian machine) */
void mcc_network_wire_order_inventories(void *entries, short count, long size);
void mcc_network_handle_inventories(void const *entries, short count);
word mcc_network_campaign_entry_size(void);
void mcc_network_handle_campaign(void const *entries, short count);
#endif
