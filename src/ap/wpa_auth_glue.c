/*
 * hostapd / WPA authenticator glue code
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "common/sae.h"
#include "common/wpa_ctrl.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "eap_server/eap.h"
#include "l2_packet/l2_packet.h"
#include "hostapd.h"
#include "ieee802_1x.h"
#include "ieee802_11_auth.h"
#include "ieee802_11.h"
#include "preauth_auth.h"
#include "sta_info.h"
#include "tkip_countermeasures.h"
#include "ap_drv_ops.h"
#include "ap_config.h"
#include "wpa_auth.h"
#include "wpa_auth_glue.h"
#include "crypto/sha1.h"
#include <stdlib.h>

#ifdef CONFIG_IEEE80211R
#include "bridge.h"
#include "dummy.h"
#include "ifconfig.h"
#endif /* CONFIG_IEEE80211R */


static void hostapd_wpa_auth_conf(struct hostapd_bss_config *conf,
				  struct hostapd_config *iconf,
				  struct wpa_auth_config *wconf)
{
	os_memset(wconf, 0, sizeof(*wconf));
	wconf->wpa = conf->wpa;
	wconf->wpa_key_mgmt = conf->wpa_key_mgmt;
	wconf->wpa_pairwise = conf->wpa_pairwise;
	wconf->wpa_group = conf->wpa_group;
	wconf->wpa_group_rekey = conf->wpa_group_rekey;
	wconf->wpa_strict_rekey = conf->wpa_strict_rekey;
	wconf->wpa_gmk_rekey = conf->wpa_gmk_rekey;
	wconf->wpa_ptk_rekey = conf->wpa_ptk_rekey;
	wconf->rsn_pairwise = conf->rsn_pairwise;
	wconf->rsn_preauth = conf->rsn_preauth;
	wconf->eapol_version = conf->eapol_version;
	wconf->peerkey = conf->peerkey;
	wconf->wmm_enabled = conf->wmm_enabled;
	wconf->wmm_uapsd = conf->wmm_uapsd;
	wconf->disable_pmksa_caching = conf->disable_pmksa_caching;
	wconf->okc = conf->okc;
#ifdef CONFIG_IEEE80211W
	wconf->ieee80211w = conf->ieee80211w;
	wconf->group_mgmt_cipher = conf->group_mgmt_cipher;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211R
	wconf->ssid_len = conf->ssid.ssid_len;
	if (wconf->ssid_len > SSID_MAX_LEN)
		wconf->ssid_len = SSID_MAX_LEN;
	os_memcpy(wconf->ssid, conf->ssid.ssid, wconf->ssid_len);
	os_memcpy(wconf->mobility_domain, conf->mobility_domain,
		  MOBILITY_DOMAIN_ID_LEN);
	if (conf->nas_identifier &&
	    os_strlen(conf->nas_identifier) <= FT_R0KH_ID_MAX_LEN) {
		wconf->r0_key_holder_len = os_strlen(conf->nas_identifier);
		os_memcpy(wconf->r0_key_holder, conf->nas_identifier,
			  wconf->r0_key_holder_len);
	}
	os_memcpy(wconf->r1_key_holder, conf->r1_key_holder, FT_R1KH_ID_LEN);
	wconf->r0_key_lifetime = conf->r0_key_lifetime;
	wconf->r1_max_key_lifetime = conf->r1_max_key_lifetime;
	wconf->reassociation_deadline = conf->reassociation_deadline;
	wconf->rkh_pos_timeout = conf->rkh_pos_timeout;
	wconf->rkh_neg_timeout = conf->rkh_neg_timeout;
	wconf->rkh_pull_timeout = conf->rkh_pull_timeout;
	wconf->rkh_pull_retries = conf->rkh_pull_retries;
	wconf->r0kh_list = &conf->r0kh_list;
	wconf->r1kh_list = &conf->r1kh_list;
	wconf->pmk_r1_push = conf->pmk_r1_push;
	wconf->ft_over_ds = conf->ft_over_ds;
	wconf->ft_psk_generate_local = conf->ft_psk_generate_local;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_HS20
	wconf->disable_gtk = conf->disable_dgaf;
	if (conf->osen) {
		wconf->disable_gtk = 1;
		wconf->wpa = WPA_PROTO_OSEN;
		wconf->wpa_key_mgmt = WPA_KEY_MGMT_OSEN;
		wconf->wpa_pairwise = 0;
		wconf->wpa_group = WPA_CIPHER_CCMP;
		wconf->rsn_pairwise = WPA_CIPHER_CCMP;
		wconf->rsn_preauth = 0;
		wconf->disable_pmksa_caching = 1;
#ifdef CONFIG_IEEE80211W
		wconf->ieee80211w = 1;
#endif /* CONFIG_IEEE80211W */
	}
#endif /* CONFIG_HS20 */
#ifdef CONFIG_TESTING_OPTIONS
	wconf->corrupt_gtk_rekey_mic_probability =
		iconf->corrupt_gtk_rekey_mic_probability;
#endif /* CONFIG_TESTING_OPTIONS */
#ifdef CONFIG_P2P
	os_memcpy(wconf->ip_addr_go, conf->ip_addr_go, 4);
	os_memcpy(wconf->ip_addr_mask, conf->ip_addr_mask, 4);
	os_memcpy(wconf->ip_addr_start, conf->ip_addr_start, 4);
	os_memcpy(wconf->ip_addr_end, conf->ip_addr_end, 4);
#endif /* CONFIG_P2P */
}


static void hostapd_wpa_auth_logger(void *ctx, const u8 *addr,
				    logger_level level, const char *txt)
{
#ifndef CONFIG_NO_HOSTAPD_LOGGER
	struct hostapd_data *hapd = ctx;
	int hlevel;

	switch (level) {
	case LOGGER_WARNING:
		hlevel = HOSTAPD_LEVEL_WARNING;
		break;
	case LOGGER_INFO:
		hlevel = HOSTAPD_LEVEL_INFO;
		break;
	case LOGGER_DEBUG:
	default:
		hlevel = HOSTAPD_LEVEL_DEBUG;
		break;
	}

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_WPA, hlevel, "%s", txt);
#endif /* CONFIG_NO_HOSTAPD_LOGGER */
}


static void hostapd_wpa_auth_disconnect(void *ctx, const u8 *addr,
					u16 reason)
{
	struct hostapd_data *hapd = ctx;
	wpa_printf(MSG_DEBUG, "%s: WPA authenticator requests disconnect: "
		   "STA " MACSTR " reason %d",
		   __func__, MAC2STR(addr), reason);
	ap_sta_disconnect(hapd, NULL, addr, reason);
}


static int hostapd_wpa_auth_mic_failure_report(void *ctx, const u8 *addr)
{
	struct hostapd_data *hapd = ctx;
	return michael_mic_failure(hapd, addr, 0);
}


static void hostapd_wpa_auth_psk_failure_report(void *ctx, const u8 *addr)
{
	struct hostapd_data *hapd = ctx;
	wpa_msg(hapd->msg_ctx, MSG_INFO, AP_STA_POSSIBLE_PSK_MISMATCH MACSTR,
		MAC2STR(addr));
}


static void hostapd_wpa_auth_set_eapol(void *ctx, const u8 *addr,
				       wpa_eapol_variable var, int value)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	if (sta == NULL)
		return;
	switch (var) {
	case WPA_EAPOL_portEnabled:
		ieee802_1x_notify_port_enabled(sta->eapol_sm, value);
		break;
	case WPA_EAPOL_portValid:
		ieee802_1x_notify_port_valid(sta->eapol_sm, value);
		break;
	case WPA_EAPOL_authorized:
		ieee802_1x_set_sta_authorized(hapd, sta, value);
		break;
	case WPA_EAPOL_portControl_Auto:
		if (sta->eapol_sm)
			sta->eapol_sm->portControl = Auto;
		break;
	case WPA_EAPOL_keyRun:
		if (sta->eapol_sm)
			sta->eapol_sm->keyRun = value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_keyAvailable:
		if (sta->eapol_sm)
			sta->eapol_sm->eap_if->eapKeyAvailable =
				value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_keyDone:
		if (sta->eapol_sm)
			sta->eapol_sm->keyDone = value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_inc_EapolFramesTx:
		if (sta->eapol_sm)
			sta->eapol_sm->dot1xAuthEapolFramesTx++;
		break;
	}
}


static int hostapd_wpa_auth_get_eapol(void *ctx, const u8 *addr,
				      wpa_eapol_variable var)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	if (sta == NULL || sta->eapol_sm == NULL)
		return -1;
	switch (var) {
	case WPA_EAPOL_keyRun:
		return sta->eapol_sm->keyRun;
	case WPA_EAPOL_keyAvailable:
		return sta->eapol_sm->eap_if->eapKeyAvailable;
	default:
		return -1;
	}
}


static const u8 * hostapd_wpa_auth_get_psk(void *ctx, const u8 *addr,
					   const u8 *p2p_dev_addr,
					   const u8 *prev_psk)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	const u8 *psk;

#ifdef CONFIG_SAE
	if (sta && sta->auth_alg == WLAN_AUTH_SAE) {
		if (!sta->sae || prev_psk)
			return NULL;
		return sta->sae->pmk;
	}
#endif /* CONFIG_SAE */

	psk = hostapd_get_psk(hapd->conf, addr, p2p_dev_addr, prev_psk);
	/*
	 * This is about to iterate over all psks, prev_psk gives the last
	 * returned psk which should not be returned again.
	 * logic list (all hostapd_get_psk; all sta->psk)
	 */
	if (sta && sta->psk && !psk) {
		struct hostapd_sta_wpa_psk_short *pos;
		psk = sta->psk->psk;
		for (pos = sta->psk; pos; pos = pos->next) {
			if (pos->ispassphrase) {
				pbkdf2_sha1(pos->passphrase,
					    hapd->conf->ssid.ssid,
					    hapd->conf->ssid.ssid_len, 4096,
					    pos->psk, PMK_LEN);
				pos->ispassphrase = 0;
			}
			if (pos->psk == prev_psk) {
				psk = pos->next ? pos->next->psk : NULL;
				break;
			}
		}
	}
	return psk;
}


static int hostapd_wpa_auth_get_msk(void *ctx, const u8 *addr, u8 *msk,
				    size_t *len)
{
	struct hostapd_data *hapd = ctx;
	const u8 *key;
	size_t keylen;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH_GET_MSK: Cannot find STA");
		return -1;
	}

	key = ieee802_1x_get_key(sta->eapol_sm, &keylen);
	if (key == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH_GET_MSK: Key is null, eapol_sm: %p",
			   sta->eapol_sm);
		return -1;
	}

	if (keylen > *len)
		keylen = *len;
	os_memcpy(msk, key, keylen);
	*len = keylen;

	return 0;
}


static int hostapd_wpa_auth_set_key(void *ctx, int vlan_id, enum wpa_alg alg,
				    const u8 *addr, int idx, u8 *key,
				    size_t key_len)
{
	struct hostapd_data *hapd = ctx;
	const char *ifname = hapd->conf->iface;

	if (vlan_id > 0) {
		ifname = hostapd_get_vlan_id_ifname(hapd->conf->vlan, vlan_id);
		if (ifname == NULL)
			return -1;
	}

	return hostapd_drv_set_key(ifname, hapd, alg, addr, idx, 1, NULL, 0,
				   key, key_len);
}


static int hostapd_wpa_auth_get_seqnum(void *ctx, const u8 *addr, int idx,
				       u8 *seq)
{
	struct hostapd_data *hapd = ctx;
	return hostapd_get_seqnum(hapd->conf->iface, hapd, addr, idx, seq);
}


static int hostapd_wpa_auth_send_eapol(void *ctx, const u8 *addr,
				       const u8 *data, size_t data_len,
				       int encrypt)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	u32 flags = 0;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_eapol_frame_io) {
		size_t hex_len = 2 * data_len + 1;
		char *hex = os_malloc(hex_len);

		if (hex == NULL)
			return -1;
		wpa_snprintf_hex(hex, hex_len, data, data_len);
		wpa_msg(hapd->msg_ctx, MSG_INFO, "EAPOL-TX " MACSTR " %s",
			MAC2STR(addr), hex);
		os_free(hex);
		return 0;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	sta = ap_get_sta(hapd, addr);
	if (sta)
		flags = hostapd_sta_flags_to_drv(sta->flags);

	return hostapd_drv_hapd_send_eapol(hapd, addr, data, data_len,
					   encrypt, flags);
}


static int hostapd_wpa_auth_for_each_sta(
	void *ctx, int (*cb)(struct wpa_state_machine *sm, void *ctx),
	void *cb_ctx)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (sta->wpa_sm && cb(sta->wpa_sm, cb_ctx))
			return 1;
	}
	return 0;
}


struct wpa_auth_iface_iter_data {
	int (*cb)(struct wpa_authenticator *sm, void *ctx);
	void *cb_ctx;
};

static int wpa_auth_iface_iter(struct hostapd_iface *iface, void *ctx)
{
	struct wpa_auth_iface_iter_data *data = ctx;
	size_t i;
	for (i = 0; i < iface->num_bss; i++) {
		if (iface->bss[i]->wpa_auth &&
		    data->cb(iface->bss[i]->wpa_auth, data->cb_ctx))
			return 1;
	}
	return 0;
}


static int hostapd_wpa_auth_for_each_auth(
	void *ctx, int (*cb)(struct wpa_authenticator *sm, void *ctx),
	void *cb_ctx)
{
	struct hostapd_data *hapd = ctx;
	struct wpa_auth_iface_iter_data data;
	if (hapd->iface->interfaces == NULL ||
	    hapd->iface->interfaces->for_each_interface == NULL)
		return -1;
	data.cb = cb;
	data.cb_ctx = cb_ctx;
	return hapd->iface->interfaces->for_each_interface(
		hapd->iface->interfaces, wpa_auth_iface_iter, &data);
}


#ifdef CONFIG_IEEE80211R

struct wpa_auth_ft_iface_iter_data {
	struct hostapd_data *src_hapd;
	const u8 *dst;
	const u8 *data;
	size_t data_len;
};


static int hostapd_wpa_auth_ft_iter(struct hostapd_iface *iface, void *ctx)
{
	struct wpa_auth_ft_iface_iter_data *idata = ctx;
	struct hostapd_data *hapd;
	size_t j;
	int multicast;

	multicast = is_multicast_ether_addr(idata->dst);

	for (j = 0; j < iface->num_bss; j++) {
		hapd = iface->bss[j];
		if (hapd == idata->src_hapd)
			continue;
		if (!hapd->wpa_auth)
			continue;
		if (os_memcmp(hapd->own_addr, idata->dst, ETH_ALEN) == 0 ||
		    multicast) {
			wpa_printf(MSG_DEBUG, "FT: Send RRB data directly to "
				   "locally managed BSS " MACSTR "@%s -> "
				   MACSTR "@%s",
				   MAC2STR(idata->src_hapd->own_addr),
				   idata->src_hapd->conf->iface,
				   MAC2STR(hapd->own_addr), hapd->conf->iface);
			wpa_ft_rrb_rx(hapd->wpa_auth,
				      idata->src_hapd->own_addr,
				      idata->data, idata->data_len);
			if (!multicast)
				return 1;
		}
	}

	return 0;
}

#endif /* CONFIG_IEEE80211R */


static int hostapd_wpa_auth_send_ether(void *ctx, const u8 *dst, u16 proto,
				       const u8 *data, size_t data_len)
{
	struct hostapd_data *hapd = ctx;
	struct l2_ethhdr *buf;
	int ret;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_eapol_frame_io && proto == ETH_P_EAPOL) {
		size_t hex_len = 2 * data_len + 1;
		char *hex = os_malloc(hex_len);

		if (hex == NULL)
			return -1;
		wpa_snprintf_hex(hex, hex_len, data, data_len);
		wpa_msg(hapd->msg_ctx, MSG_INFO, "EAPOL-TX " MACSTR " %s",
			MAC2STR(dst), hex);
		os_free(hex);
		return 0;
	}
#endif /* CONFIG_TESTING_OPTIONS */

#ifdef CONFIG_IEEE80211R
	if (proto == ETH_P_RRB && hapd->iface->interfaces &&
	    hapd->iface->interfaces->for_each_interface) {
		int res;
		struct wpa_auth_ft_iface_iter_data idata;
		idata.src_hapd = hapd;
		idata.dst = dst;
		idata.data = data;
		idata.data_len = data_len;
		res = hapd->iface->interfaces->for_each_interface(
			hapd->iface->interfaces, hostapd_wpa_auth_ft_iter,
			&idata);
		if (res == 1)
			return data_len;
	}
#endif /* CONFIG_IEEE80211R */

	if (hapd->driver && hapd->driver->send_ether)
		return hapd->driver->send_ether(hapd->drv_priv, dst,
						hapd->own_addr, proto,
						data, data_len);
	if (hapd->l2 == NULL)
		return -1;

	buf = os_malloc(sizeof(*buf) + data_len);
	if (buf == NULL)
		return -1;
	os_memcpy(buf->h_dest, dst, ETH_ALEN);
	os_memcpy(buf->h_source, hapd->own_addr, ETH_ALEN);
	buf->h_proto = host_to_be16(proto);
	os_memcpy(buf + 1, data, data_len);
	ret = l2_packet_send(hapd->l2, dst, proto, (u8 *) buf,
			     sizeof(*buf) + data_len);
	os_free(buf);
	return ret;
}


#ifdef CONFIG_IEEE80211R

static int hostapd_wpa_auth_send_ft_action(void *ctx, const u8 *dst,
					   const u8 *data, size_t data_len)
{
	struct hostapd_data *hapd = ctx;
	int res;
	struct ieee80211_mgmt *m;
	size_t mlen;
	struct sta_info *sta;
	int ifidx;

	sta = ap_get_sta(hapd, dst);
	if (sta == NULL || sta->wpa_sm == NULL)
		return -1;

	m = os_zalloc(sizeof(*m) + data_len);
	if (m == NULL)
		return -1;
	mlen = ((u8 *) &m->u - (u8 *) m) + data_len;
	m->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					WLAN_FC_STYPE_ACTION);
	os_memcpy(m->da, dst, ETH_ALEN);
	os_memcpy(m->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(m->bssid, hapd->own_addr, ETH_ALEN);
	os_memcpy(&m->u, data, data_len);

	ifidx = hostapd_get_sta_ifidx(hapd, dst);

	res = hostapd_drv_send_mlme_ifidx(hapd, (u8 *) m, mlen, 0, ifidx);
	os_free(m);
	return res;
}


static struct wpa_state_machine *
hostapd_wpa_auth_add_sta(struct hostapd_data *hapd, const u8 *sta_addr)
{
	struct sta_info *sta;

	if (hostapd_add_sta_node(hapd, sta_addr, WLAN_AUTH_FT) < 0)
		return NULL;

	sta = ap_sta_add(hapd, sta_addr);
	if (sta == NULL)
		return NULL;
	if (sta->wpa_sm) {
		sta->auth_alg = WLAN_AUTH_FT;
		return sta->wpa_sm;
	}

	sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth, sta->addr, NULL);
	if (sta->wpa_sm == NULL) {
		ap_free_sta(hapd, sta);
		return NULL;
	}
	sta->auth_alg = WLAN_AUTH_FT;

	return sta->wpa_sm;
}


static int
hostapd_wpa_auth_add_sta_auth(void *ctx, const u8 *sta_addr,
			      struct wpa_state_machine **sm,
			      void (*cb)(void *hapd, const u8 *buf, size_t len, const u8 *mac, int accepted, u32 session_timeout),
			      void *cb_ctx, int cb_ctx_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = NULL;
	int res = 0;
	void (*cb2) (struct hostapd_data *hapd, const u8 *buf, size_t len, const u8 *mac, int accepted, u32 session_timeout);
	struct hostapd_allowed_address_info info;

	hostapd_allowed_address_init(&info);
	cb2 = (void (*) (struct hostapd_data *hapd, const u8 *buf, size_t len, const u8 *mac, int accepted, u32 session_timeout)) cb;

	if (os_memcmp(sta_addr, hapd->own_addr, ETH_ALEN) == 0) {
		wpa_printf(MSG_INFO, "Station " MACSTR " not allowed to authenticate",
			   MAC2STR(sta_addr));
		*sm = NULL;
		return 0;
	}

	*sm = hostapd_wpa_auth_add_sta(hapd, sta_addr);
	if (!*sm)
		return 0;

	res = hostapd_allowed_address(hapd, sta_addr, (u8 *) cb_ctx,
				      cb_ctx_len, cb2, &info);

	if (res == HOSTAPD_ACL_REJECT) {
		wpa_printf(MSG_INFO, "Station " MACSTR " not allowed to authenticate",
			   MAC2STR(sta_addr));
		goto fail;
	}
	if (res == HOSTAPD_ACL_PENDING) {
		wpa_printf(MSG_DEBUG, "Authentication frame from " MACSTR
			   " waiting for an external authentication",
			   MAC2STR(sta_addr));
		/* Authentication code will re-send the authentication frame
		 * after it has received (and cached) information from the
		 * external source. */
		res = -1;
		goto fail;
	}

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		goto fail;

	if (handle_auth_cfg_sta(hapd, sta, res, &info, NULL) < 0)
		goto fail;

	sta->flags &= ~WLAN_STA_PREAUTH;
	sta->flags |= WLAN_STA_PREAUTH_FT_OVER_DS;

	ieee802_1x_notify_pre_auth(sta->eapol_sm, 0);

	return 0;
fail:
	hostapd_allowed_address_free(&info);
	*sm = NULL;

	return res;
}


static void hostapd_wpa_vlan_to_ft(struct ft_vlan *ft_vlan,
				   struct vlan_description vlan_desc)
{
	int i,j;

	os_memset(ft_vlan, 0, sizeof(*ft_vlan));
	ft_vlan->untagged = host_to_le16(vlan_desc.untagged);
	for (i=0, j=0;
	     i < FT_MAX_NUM_TAGGED_VLAN && j < MAX_NUM_TAGGED_VLAN;
	     j++) {
		if (!vlan_desc.tagged[j])
			break;
		ft_vlan->tagged[i] = host_to_le16(vlan_desc.tagged[j]);
		i++;
	}
}


static int hostapd_wpa_ft_to_vlan(struct hostapd_data *hapd,
				  struct vlan_description *vlan_desc,
				  struct ft_vlan ft_vlan)
{
	int i,j;

	/* convert ft_vlan into vlan_description */
	os_memset(vlan_desc, 0, sizeof(*vlan_desc));
	vlan_desc->untagged = le_to_host16(ft_vlan.untagged);
	for (i=0, j=0;
	     i < FT_MAX_NUM_TAGGED_VLAN && j < MAX_NUM_TAGGED_VLAN;
	     i++) {
		if (!ft_vlan.tagged[i])
			break;
		vlan_desc->tagged[j] = le_to_host16(ft_vlan.tagged[j]);
		j++;
	}
	vlan_desc->notempty = vlan_desc->untagged || vlan_desc->tagged[0];

	/* fail early so that does not get connected if invalid vlan */
	if (vlan_desc->notempty &&
	    !hostapd_vlan_id_valid(hapd->conf->vlan, *vlan_desc))
		return -1;
	return 0;
}


static int hostapd_wpa_auth_set_vlan(void *ctx, const u8 *sta_addr,
				     struct ft_vlan ft_vlan)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	struct vlan_description vlan_desc;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return -1;

	if (!sta->wpa_sm)
		return -1;

	if (hostapd_wpa_ft_to_vlan(hapd, &vlan_desc, ft_vlan) < 0) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "Invalid VLAN "
			       "%d%s received from FT",
			       vlan_desc.untagged,
			       vlan_desc.tagged[0] ? "+" : "");
		return -1;
	}

	if (ap_sta_set_vlan(hapd, sta, vlan_desc) < 0)
		return -1;
	/* configure wpa_group for GTK but ignore error due to driver not
	 * knowing this sta */
	ap_sta_bind_vlan(hapd, sta);

	if (sta->vlan_id)
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "VLAN ID %d", sta->vlan_id);

	return 0;
}


static int
hostapd_wpa_auth_get_vlan(void *ctx, const u8 *sta_addr,
			  struct ft_vlan *ft_vlan)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return -1;

	hostapd_wpa_vlan_to_ft(ft_vlan, sta->vlan_desc);
	return 0;
}


static int
hostapd_wpa_auth_get_session_timeout(void *ctx, const u8 *sta_addr)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return 0;

	if (!sta->session_timeout_set)
		return 0;

	return sta->session_timeout;
}


static size_t
hostapd_wpa_auth_get_identity(void *ctx, const u8 *sta_addr, u8 *buf, size_t buflen)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	size_t len;
	u8 *b;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return 0;

	b = ieee802_1x_get_identity(sta->eapol_sm, &len);
	if (b && len) {
		if (len >= buflen)
			len = buflen - 1;
		os_memcpy(buf, b, len);
		buf[len] = 0;
		return len;
	}

	if (!sta->identity)
		return 0;

	len = os_strlen(sta->identity);
	if (len >= buflen)
		len = buflen - 1;
	os_memcpy(buf, sta->identity, len);
	buf[len] = 0;
	
	return len;
}


static size_t
hostapd_wpa_auth_get_radius_cui(void *ctx, const u8 *sta_addr, u8 *buf, size_t buflen)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	struct wpabuf *b;
	size_t len;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return 0;

	b = ieee802_1x_get_radius_cui(sta->eapol_sm);
	if (b) {
		len = wpabuf_len(b);
		if (len >= buflen)
			len = buflen - 1;
		os_memcpy(buf, wpabuf_head(b), len);
		buf[len] = 0;
		return len;
	}

	if (!sta->radius_cui)
		return 0;

	len = os_strlen(sta->radius_cui);
	if (len >= buflen)
		len = buflen - 1;
	os_memcpy(buf, sta->radius_cui, len);
	buf[len] = 0;
	
	return len;
}


static void
hostapd_wpa_auth_set_session_timeout(void *ctx, const u8 *sta_addr, int session_timeout)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return;

	sta->session_timeout = session_timeout;
	sta->session_timeout_set = !!session_timeout;

	if (sta->session_timeout_set)
		ap_sta_session_timeout(hapd, sta, sta->session_timeout);
	else
		ap_sta_no_session_timeout(hapd, sta);
}


static void
hostapd_wpa_auth_set_identity(void *ctx, const u8 *sta_addr, u8 *identity, size_t identity_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return;

	if (sta->identity) {
		os_free(sta->identity);
		sta->identity = NULL;
	}

	if (sta->eapol_sm && sta->eapol_sm->identity) {
		os_free(sta->eapol_sm->identity);
		sta->eapol_sm->identity_len = 0;
	}

	if (!identity_len)
		return;

	/* sta->identity is NULL terminated */
	sta->identity = os_zalloc(identity_len + 1);
	if (sta->identity) {
		os_memcpy(sta->identity, identity, identity_len);
	}

	if (sta->eapol_sm) {
		sta->eapol_sm->identity = os_zalloc(identity_len);
		if (sta->eapol_sm->identity) {
			os_memcpy(sta->eapol_sm->identity, identity, identity_len);
			sta->eapol_sm->identity_len = identity_len;
		}
	}
}


static void
hostapd_wpa_auth_set_radius_cui(void *ctx, const u8 *sta_addr, u8 *radius_cui, size_t radius_cui_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL)
		return;

	if (sta->radius_cui) {
		os_free(sta->radius_cui);
		sta->radius_cui = NULL;
	}

	if (sta->eapol_sm && sta->eapol_sm->radius_cui) {
		wpabuf_free(sta->eapol_sm->radius_cui);
		sta->eapol_sm->radius_cui = NULL;
	}

	if (!radius_cui)
		return;

	/* sta->radius_cui is NULL terminated */
	sta->radius_cui = os_zalloc(radius_cui_len + 1);
	if (sta->radius_cui)
		os_memcpy(sta->radius_cui, radius_cui, radius_cui_len);

	if (sta->eapol_sm)
		sta->eapol_sm->radius_cui = wpabuf_alloc_copy(radius_cui,
							      radius_cui_len);
}


static void hostapd_rrb_receive(void *ctx, const u8 *src_addr, const u8 *buf,
				size_t len)
{
	struct hostapd_data *hapd = ctx;
	struct l2_ethhdr *ethhdr;
	if (len < sizeof(*ethhdr))
		return;
	ethhdr = (struct l2_ethhdr *) buf;
	wpa_printf(MSG_DEBUG, "FT: RRB received packet " MACSTR " -> "
		   MACSTR, MAC2STR(ethhdr->h_source), MAC2STR(ethhdr->h_dest));
	if ((!is_multicast_ether_addr(ethhdr->h_dest)) && 
	    (os_memcmp(hapd->own_addr, ethhdr->h_dest, ETH_ALEN) != 0))
		return;
	wpa_ft_rrb_rx(hapd->wpa_auth, ethhdr->h_source, buf + sizeof(*ethhdr),
		      len - sizeof(*ethhdr));
}


static int hostapd_wpa_auth_add_tspec(void *ctx, const u8 *sta_addr,
				      u8 *tspec_ie, size_t tspec_ielen)
{
	struct hostapd_data *hapd = ctx;
	return hostapd_add_tspec(hapd, sta_addr, tspec_ie, tspec_ielen);
}

#endif /* CONFIG_IEEE80211R */


int hostapd_setup_wpa(struct hostapd_data *hapd)
{
	struct wpa_auth_config _conf;
	struct wpa_auth_callbacks cb;
	const u8 *wpa_ie;
	size_t wpa_ie_len;
#ifdef CONFIG_IEEE80211R
	const char* ft_iface;
#ifdef CONFIG_LIBNL3_ROUTE
	char dummy_iface[IFNAMSIZ+1];
#endif /* CONFIG_LIBNL3_ROUTE */
#endif /* CONFIG_IEEE80211R */

	hostapd_wpa_auth_conf(hapd->conf, hapd->iconf, &_conf);
	if (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_EAPOL_TX_STATUS)
		_conf.tx_status = 1;
	if (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_AP_MLME)
		_conf.ap_mlme = 1;
	os_memset(&cb, 0, sizeof(cb));
	cb.ctx = hapd;
	cb.logger = hostapd_wpa_auth_logger;
	cb.disconnect = hostapd_wpa_auth_disconnect;
	cb.mic_failure_report = hostapd_wpa_auth_mic_failure_report;
	cb.psk_failure_report = hostapd_wpa_auth_psk_failure_report;
	cb.set_eapol = hostapd_wpa_auth_set_eapol;
	cb.get_eapol = hostapd_wpa_auth_get_eapol;
	cb.get_psk = hostapd_wpa_auth_get_psk;
	cb.get_msk = hostapd_wpa_auth_get_msk;
	cb.set_key = hostapd_wpa_auth_set_key;
	cb.get_seqnum = hostapd_wpa_auth_get_seqnum;
	cb.send_eapol = hostapd_wpa_auth_send_eapol;
	cb.for_each_sta = hostapd_wpa_auth_for_each_sta;
	cb.for_each_auth = hostapd_wpa_auth_for_each_auth;
	cb.send_ether = hostapd_wpa_auth_send_ether;
#ifdef CONFIG_IEEE80211R
	cb.send_ft_action = hostapd_wpa_auth_send_ft_action;
	cb.add_sta = hostapd_wpa_auth_add_sta_auth;
	cb.set_vlan = hostapd_wpa_auth_set_vlan;
	cb.get_vlan = hostapd_wpa_auth_get_vlan;
	cb.get_session_timeout = hostapd_wpa_auth_get_session_timeout;
	cb.get_identity = hostapd_wpa_auth_get_identity;
	cb.get_radius_cui = hostapd_wpa_auth_get_radius_cui;
	cb.set_session_timeout = hostapd_wpa_auth_set_session_timeout;
	cb.set_identity = hostapd_wpa_auth_set_identity;
	cb.set_radius_cui = hostapd_wpa_auth_set_radius_cui;
	cb.add_tspec = hostapd_wpa_auth_add_tspec;
#endif /* CONFIG_IEEE80211R */
	hapd->wpa_auth = wpa_init(hapd->own_addr, &_conf, &cb);
	if (hapd->wpa_auth == NULL) {
		wpa_printf(MSG_ERROR, "WPA initialization failed.");
		return -1;
	}

	if (hostapd_set_privacy(hapd, 1)) {
		wpa_printf(MSG_ERROR, "Could not set PrivacyInvoked "
			   "for interface %s", hapd->conf->iface);
		return -1;
	}

	wpa_ie = wpa_auth_get_wpa_ie(hapd->wpa_auth, &wpa_ie_len);
	if (hostapd_set_generic_elem(hapd, wpa_ie, wpa_ie_len)) {
		wpa_printf(MSG_ERROR, "Failed to configure WPA IE for "
			   "the kernel driver.");
		return -1;
	}

	if (rsn_preauth_iface_init(hapd)) {
		wpa_printf(MSG_ERROR, "Initialization of RSN "
			   "pre-authentication failed.");
		return -1;
	}

#ifdef CONFIG_IEEE80211R
	if (!hostapd_drv_none(hapd)) {
		ft_iface = hapd->conf->iface;
		if (hapd->conf->bridge[0])
			ft_iface = hapd->conf->bridge;
		if (hapd->conf->ft_bridge[0]) {
			ft_iface = hapd->conf->ft_bridge;
#ifdef CONFIG_LIBNL3_ROUTE
			snprintf(dummy_iface, sizeof(dummy_iface), "ft%s",
				 hapd->conf->iface);
			if (dummy_add(dummy_iface, hapd->own_addr) < 0 ||
			    ifconfig_up(dummy_iface) < 0 ||
			    br_addif(ft_iface, dummy_iface) < 0)
				wpa_printf(MSG_ERROR, "Failed to add bssid to "
					   "ft_bridge %s", ft_iface);
#else
			wpa_printf(MSG_ERROR, "Missing libnl3 - bssid not added"
				   " to ft_bridge %s", ft_iface);
#endif /* CONFIG_LIBNL3_ROUTE */
		}
		hapd->l2 = l2_packet_init(ft_iface, NULL, ETH_P_RRB,
					  hostapd_rrb_receive, hapd, 1);
		if (hapd->l2 == NULL &&
		    (hapd->driver == NULL ||
		     hapd->driver->send_ether == NULL)) {
			wpa_printf(MSG_ERROR, "Failed to open l2_packet "
				   "interface");
			return -1;
		}
	}
#endif /* CONFIG_IEEE80211R */

	return 0;

}

int hostapd_setup_wpa_vlan(struct hostapd_data *hapd, int vlan_id)
{
	return wpa_auth_ensure_group(hapd->wpa_auth, vlan_id);
}

int hostapd_desetup_wpa_vlan(struct hostapd_data *hapd, int vlan_id)
{
	return wpa_auth_release_group(hapd->wpa_auth, vlan_id);
}

void hostapd_reconfig_wpa(struct hostapd_data *hapd)
{
	struct wpa_auth_config wpa_auth_conf;
	hostapd_wpa_auth_conf(hapd->conf, hapd->iconf, &wpa_auth_conf);
	wpa_reconfig(hapd->wpa_auth, &wpa_auth_conf);
}


void hostapd_deinit_wpa(struct hostapd_data *hapd)
{
#ifdef CONFIG_LIBNL3_ROUTE
	char dummy_iface[IFNAMSIZ+1];
#endif /* CONFIG_LIBNL3_ROUTE */

	ieee80211_tkip_countermeasures_deinit(hapd);
	rsn_preauth_iface_deinit(hapd);
	if (hapd->wpa_auth) {
		wpa_deinit(hapd->wpa_auth);
		hapd->wpa_auth = NULL;

		if (hostapd_set_privacy(hapd, 0)) {
			wpa_printf(MSG_DEBUG, "Could not disable "
				   "PrivacyInvoked for interface %s",
				   hapd->conf->iface);
		}

		if (hostapd_set_generic_elem(hapd, (u8 *) "", 0)) {
			wpa_printf(MSG_DEBUG, "Could not remove generic "
				   "information element from interface %s",
				   hapd->conf->iface);
		}
	}
	ieee802_1x_deinit(hapd);

#ifdef CONFIG_IEEE80211R
#ifdef CONFIG_LIBNL3_ROUTE
	if (hapd->conf->ft_bridge[0]) {
		snprintf(dummy_iface, sizeof(dummy_iface), "ft%s",
			 hapd->conf->iface);
		ifconfig_down(dummy_iface);
		br_delif(hapd->conf->ft_bridge, dummy_iface);
		dummy_del(dummy_iface);
	}
#endif /* CONFIG_LIBNL3_ROUTE */
	l2_packet_deinit(hapd->l2);
	hapd->l2 = NULL;
#endif /* CONFIG_IEEE80211R */
}
