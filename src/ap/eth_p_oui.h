/*
 * hostapd / IEEE 802 OUI Extended Ethertype
 * Copyright (c) 2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef ETH_P_OUI_H
#define ETH_P_OUI_H

struct eth_p_oui_ctx;
struct hostapd_data;

#ifdef CONFIG_ETH_P_OUI

/* rx_callback only gets payload after OUI passed as buf */
struct eth_p_oui_ctx *
eth_p_oui_register(struct hostapd_data *hapd, const char *ifname, u8 ouisuffix,
		   void (*rx_callback)(void *ctx, const u8 *src_addr,
				       const u8 *dst_addr, u8 ouisuffix,
				       const u8 *buf, size_t len),
		   void *rx_callback_ctx);
void eth_p_oui_unregister(struct eth_p_oui_ctx *eth_p_oui);
int eth_p_oui_send(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
		   const u8 *dst_addr, const u8 *buf, size_t len);
void eth_p_oui_deliver(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
		       const u8 *dst_addr, const u8 *buf, size_t len);

#else /* CONFIG_ETH_P_OUI */

static inline struct eth_p_oui_ctx *
eth_p_oui_register(struct hostapd_data *hapd, const char *ifname, u8 ouisuffix,
		   void (*rx_callback)(void *ctx, const u8 *src_addr,
				       const u8 *dst_addr, u8 ouisuffix,
				       const u8 *buf, size_t len),
		   void *rx_callback_ctx)
{
	return NULL;
}

static inline void eth_p_oui_unregister(struct eth_p_oui_ctx *ctx)
{
}

static inline int
eth_p_oui_send(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
	       const u8 *dst_addr, const u8 *buf, size_t len)
{
	return -1;
}

static inline void
eth_p_oui_deliver(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
		  const u8 *dst_addr, const u8 *buf, size_t len)
{
}

#endif /* CONFIG_ETH_P_OUI */

#endif /* ETH_P_OUI_H */
