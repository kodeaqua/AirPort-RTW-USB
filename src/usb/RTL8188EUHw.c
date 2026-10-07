/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8188EUHw.c - mac80211-shaped face over the hardware-proven RTL8188EUCore.
 *
 * Compiled as part of AirPortRTW (C, compat headers, same flags as src/compat/*.c). It allocates an
 * ieee80211_hw whose ops call the core through rtl8188eu_bridge.h, and registers an rtw88_core_ops table so
 * RTW88IEEE80211 (the existing IO80211 frontend) drives this device through the same hw->ops contract it uses
 * for rtw88. Register sequences are NOT here: every hardware action is one bridge call into the core, whose
 * steps are ported from Linux rtl8xxxu (see docs/stage3c-init.md, docs/stage4-plan.md).
 *
 * Scope of this first slice (everything else returns -EOPNOTSUPP or is a documented no-op):
 *   - 2.4 GHz channels 1-13, legacy (CCK/OFDM) rates and HT20 MCS0-7 (no A-MPDU aggregation on TX, no 40 MHz, no SGI)
 *   - software scan (frontend sw_scan + channel hop); no firmware scan
 *   - CCMP only (set_key returns -EOPNOTSUPP for TKIP/WEP, so such networks cannot be joined)
 *   - data rate from the core's software rate adaptation (RSSI start rate + TX reports); mgmt at 1M
 *   - no TX status reporting (frames are copied into the core's TX buffer and the skb is freed in ops->tx)
 */

#include "rtw88_compat.h"
#include "rtw88_core_ops.h"
#include "rtl8188eu_bridge.h"

#define R8188EU_NCHAN  13
#define R8188EU_NRATE  12

struct r8188eu_hw {
    void                              *core;
    struct ieee80211_hw               *hw;
    struct ieee80211_supported_band    band;
    struct ieee80211_channel           chans[R8188EU_NCHAN];
    struct ieee80211_rate              rates[R8188EU_NRATE];
    struct sk_buff_head                rxq;
    struct work_struct                 rx_work;
    bool                               hw_inited;
    bool                               started;
    volatile int                       scanning;
    int                                cur_ch;
    uint8_t                            mac[ETH_ALEN];
    uint8_t                            tx_nss;
};

static struct r8188eu_hw g_r8;
static void *g_r8_core;

void rtl8188eu_hw_set_core(void *core) { g_r8_core = core; }
void rtl8188eu_hw_invalidate_init(void) { g_r8.hw_inited = false; }

/* ------------------------------------------------------------------ */
/*  RX: USB completion context -> queue -> worker -> frontend           */
/* ------------------------------------------------------------------ */

static void r8_rx_work(struct work_struct *w)
{
    struct sk_buff *skb;
    while ((skb = skb_dequeue(&g_r8.rxq)) != NULL)
        ieee80211_rx_irqsafe(g_r8.hw, skb);
}

/* Runs on the USB workloop: must not sleep. Copy and defer. */
static void r8_rx_cb(void *ctx, const uint8_t *frame, uint32_t len, bool decrypted, bool has_signal, int8_t signal_dbm)
{
    struct sk_buff *skb;
    struct ieee80211_rx_status *rxs;

    if (!g_r8.started || !frame || len < 10 || len > 4096)
        return;
    skb = alloc_skb(len, GFP_ATOMIC);
    if (!skb)
        return;
    skb_put_data(skb, frame, len);
    rxs = IEEE80211_SKB_RXCB(skb);
    memset(rxs, 0, sizeof(*rxs));
    rxs->freq = (u16)(2407 + 5 * g_r8.cur_ch);
    rxs->band = NL80211_BAND_2GHZ;
    if (has_signal)
        rxs->signal = signal_dbm;            /* from phystats, see RTL8188EUCore::parseRx */
    else
        rxs->flag = RX_FLAG_NO_SIGNAL_VAL;
    if (decrypted)
        rxs->flag |= RX_FLAG_DECRYPTED;
    skb_queue_tail(&g_r8.rxq, skb);
    schedule_work(&g_r8.rx_work);
}

/* ------------------------------------------------------------------ */
/*  ieee80211_ops                                                       */
/* ------------------------------------------------------------------ */

static int r8_set_channel_from_conf(struct ieee80211_hw *hw)
{
    struct ieee80211_channel *c = hw->conf.chandef.chan;
    int r;
    if (!c || c->band != NL80211_BAND_2GHZ || c->hw_value < 1 || c->hw_value > R8188EU_NCHAN)
        return -EINVAL;
    if (c->hw_value == g_r8.cur_ch)
        return 0;
    r = rtl8188eu_br_set_channel(g_r8.core, c->hw_value);
    if (r) return -EIO;
    g_r8.cur_ch = c->hw_value;
    return 0;
}

static void r8_ops_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb)
{
    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
    struct rtl8188eu_br_txparams p;
    struct ieee80211_key_conf *key = info->control.hw_key;
    unsigned ac = skb_get_queue_mapping(skb);

    memset(&p, 0, sizeof(p));
    p.ac = ac > 3 ? 2 : (uint8_t)ac;
    p.ccmp = key && key->cipher == WLAN_CIPHER_SUITE_CCMP;
    p.hw_key_idx = key ? key->hw_key_idx : 0xff;
    p.short_preamble = info->control.short_preamble;
    p.use_rts = info->control.use_rts;
    p.use_cts_self = info->control.use_cts_prot;
    /* Errors (no free TX buffer, oversize) drop the frame, like a full ring would. */
    (void)rtl8188eu_br_tx(g_r8.core, skb->data, (uint16_t)skb->len, &p, NULL);
    kfree_skb(skb);
}

static int r8_ops_start(struct ieee80211_hw *hw)
{
    int r;
    if (!g_r8.hw_inited) {
        r = rtl8188eu_br_init_hw(g_r8.core);
        if (r) return -EIO;
        g_r8.hw_inited = true;
    }
    g_r8.started = true;
    r = rtl8188eu_br_async_start(g_r8.core, r8_rx_cb, NULL, &g_r8);
    if (r) { g_r8.started = false; return -EIO; }
    g_r8.cur_ch = 0;
    return r8_set_channel_from_conf(hw);
}

static void r8_ops_stop(struct ieee80211_hw *hw, bool suspend)
{
    g_r8.started = false;
    rtl8188eu_br_async_stop(g_r8.core);
    cancel_work_sync(&g_r8.rx_work);
    skb_queue_purge(&g_r8.rxq);
}

static int r8_ops_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    return rtl8188eu_br_add_station_if(g_r8.core) ? -EIO : 0;
}

static void r8_ops_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif) {}

static int r8_ops_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
    if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
        return r8_set_channel_from_conf(hw);
    return 0;
}

/* RCR is set once in init_wmac (accept phys-match, mcast, bcast, mgmt); no per-flag filtering yet. */
static void r8_ops_configure_filter(struct ieee80211_hw *hw, unsigned int changed, unsigned int *total, u64 mc)
{
    *total &= FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL | FIF_OTHER_BSS | FIF_PROBE_REQ;
}

/* Peer rate set -> software rate adaptation (rtl8xxxu bss_info_changed ASSOC: ramask = supp_rates[0] & 0xfff | mcs.rx_mask[0] << 12). */
static int r8_ops_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
    bool ht = sta->deflink.ht_cap.ht_supported;
    rtl8188eu_br_set_peer(g_r8.core, (uint32_t)sta->deflink.supp_rates[NL80211_BAND_2GHZ] & 0xfff,
                          ht ? sta->deflink.ht_cap.mcs.rx_mask[0] : 0, ht);
    return 0;
}
static int r8_ops_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
    rtl8188eu_br_clear_peer(g_r8.core);
    return 0;
}

/* bss_conf->basic_rates is a bitmap over band->bitrates[] indexes, which here equals the rtl rate order. */
static void r8_ops_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                                    struct ieee80211_bss_conf *info, u64 changed)
{
    void *core = g_r8.core;
    if ((changed & BSS_CHANGED_BSSID) && info->bssid)
        rtl8188eu_br_set_bssid(core, info->bssid);
    if (changed & BSS_CHANGED_BASIC_RATES)
        rtl8188eu_br_set_basic_rates(core, info->basic_rates);
    if (changed & BSS_CHANGED_ERP_PREAMBLE)
        rtl8188eu_br_set_short_preamble(core, info->use_short_preamble);
    if (changed & BSS_CHANGED_ERP_SLOT)
        rtl8188eu_br_set_slot(core, info->use_short_slot, false);
    if (changed & BSS_CHANGED_ASSOC) {
        if (info->assoc) rtl8188eu_br_join(core, info->aid);
        else             rtl8188eu_br_leave(core);
    }
}

static int r8_ops_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd, struct ieee80211_vif *vif,
                          struct ieee80211_sta *sta, struct ieee80211_key_conf *key)
{
    if (cmd == DISABLE_KEY)
        return rtl8188eu_br_clear_key(g_r8.core, key->hw_key_idx) ? -EIO : 0;
    if (key->cipher != WLAN_CIPHER_SUITE_CCMP || key->keylen != 16)
        return -EOPNOTSUPP;    /* TKIP/WEP/BIP not ported; frontend then fails the join */
    {
        uint8_t hw_idx = 0xff;
        bool pairwise = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0;
        const uint8_t *mac = sta ? sta->addr : (vif->bss_conf.bssid ? vif->bss_conf.bssid : vif->addr);
        if (rtl8188eu_br_set_key_ccmp(g_r8.core, (uint8_t)key->keyidx, pairwise, mac, key->key, &hw_idx))
            return -EIO;
        key->hw_key_idx = hw_idx;
    }
    return 0;
}

static const struct ieee80211_ops r8188eu_ops = {
    .tx               = r8_ops_tx,
    .start            = r8_ops_start,
    .stop             = r8_ops_stop,
    .add_interface    = r8_ops_add_interface,
    .remove_interface = r8_ops_remove_interface,
    .config           = r8_ops_config,
    .configure_filter = r8_ops_configure_filter,
    .sta_add          = r8_ops_sta_add,
    .sta_remove       = r8_ops_sta_remove,
    .bss_info_changed = r8_ops_bss_info_changed,
    .set_key          = r8_ops_set_key,
    /* hw_scan / cancel_hw_scan intentionally NULL: frontend falls back to sw scan (rtw88_hw_scan_supported = false). */
};

/* ------------------------------------------------------------------ */
/*  rtw88_core_ops                                                      */
/* ------------------------------------------------------------------ */

static void r8_fill_bands(void)
{
    static const u16 bitrate[R8188EU_NRATE] = { 10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540 };
    int i;
    for (i = 0; i < R8188EU_NCHAN; i++) {
        g_r8.chans[i].band = NL80211_BAND_2GHZ;
        g_r8.chans[i].center_freq = (u16)(2412 + 5 * i);
        g_r8.chans[i].hw_value = (u16)(i + 1);
        g_r8.chans[i].max_power = 20;                 /* GUESS: matches the txpower the core programs */
        g_r8.chans[i].max_reg_power = 20;
    }
    for (i = 0; i < R8188EU_NRATE; i++) {
        g_r8.rates[i].bitrate = bitrate[i];
        g_r8.rates[i].hw_value = (u16)i;              /* rtl rate index = bit position in basic_rates */
        if (i == 1 || i == 2 || i == 3)
            g_r8.rates[i].flags = IEEE80211_RATE_SHORT_PREAMBLE;
    }
    g_r8.band.band = NL80211_BAND_2GHZ;
    g_r8.band.channels = g_r8.chans;
    g_r8.band.n_channels = R8188EU_NCHAN;
    g_r8.band.bitrates = g_r8.rates;
    g_r8.band.n_bitrates = R8188EU_NRATE;
    /* HT20, 1 stream, MCS0-7, long GI, no STBC/LDPC/greenfield, no 40 MHz (TX descriptor has no HT40/SGI/A-MPDU bits ported).
     * GUESS: A-MPDU parameters; the chip delivers each MPDU as its own bulk IN packet (RX aggregation off), TX never aggregates
     * (AGG_BREAK), the frontend still negotiates BlockAck and reorders downlink A-MPDUs. */
    g_r8.band.ht_cap.ht_supported = true;
    g_r8.band.ht_cap.cap = 0x000c;                        /* SM power save: disabled (value 3 << 2) */
    g_r8.band.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K;
    g_r8.band.ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;
    g_r8.band.ht_cap.mcs.rx_mask[0] = 0xff;               /* MCS0-7 */
    g_r8.band.ht_cap.mcs.rx_highest = 65;                 /* Mbps, MCS7 20 MHz long GI */
    g_r8.band.ht_cap.mcs.tx_params = 1;                   /* IEEE80211_HT_MCS_TX_DEFINED */
}

static int r8_probe(void *transport)
{
    struct ieee80211_hw *hw;
    if (!g_r8_core) return -ENODEV;
    memset(&g_r8, 0, sizeof(g_r8));
    g_r8.core = g_r8_core;
    g_r8.tx_nss = 1;
    skb_queue_head_init(&g_r8.rxq);
    INIT_WORK(&g_r8.rx_work, r8_rx_work);

    /* MAC address needs the efuse, which is read as part of the init chain; run it now so
     * the frontend sees the real perm_addr right after probe (start() then only starts RX). */
    if (rtl8188eu_br_init_hw(g_r8.core)) return -EIO;
    g_r8.hw_inited = true;
    rtl8188eu_br_get_mac(g_r8.core, g_r8.mac);

    /* hw->priv is never an rtw_dev here; 256 spare bytes so a stray cast cannot run off the allocation. */
    hw = ieee80211_alloc_hw(256, &r8188eu_ops);
    if (!hw) return -ENOMEM;
    g_r8.hw = hw;
    r8_fill_bands();
    hw->wiphy->bands[NL80211_BAND_2GHZ] = &g_r8.band;
    hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
    hw->wiphy->available_antennas_tx = 1;
    hw->wiphy->available_antennas_rx = 1;
    memcpy(hw->wiphy->perm_addr, g_r8.mac, ETH_ALEN);
    hw->queues = 4;
    hw->max_rates = 1;
    hw->extra_tx_headroom = 0;
    hw->vif_data_size = 0;
    hw->sta_data_size = 0;
    ieee80211_hw_set(hw, SIGNAL_DBM);
    /* NOT RX_INCLUDES_FCS: rtl8xxxu does not set it either (RCR has no append-CRC; pktlen excludes FCS). */
    rtw88_register_hw(hw);
    return 0;
}

static void r8_remove(void *transport)
{
    if (g_r8.hw) {
        r8_ops_stop(g_r8.hw, false);
        ieee80211_free_hw(g_r8.hw);     /* also unregisters from the compat layer */
        g_r8.hw = NULL;
    }
    g_r8.hw_inited = false;
}

static bool r8_is_scanning(void) { return g_r8.scanning != 0; }
static bool r8_hw_scan_supported(struct ieee80211_hw *hw) { return false; }
static void r8_sw_scan_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { g_r8.scanning = 1; }
static void r8_sw_scan_switch_channel(struct ieee80211_hw *hw) { (void)r8_set_channel_from_conf(hw); }
static void r8_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { g_r8.scanning = 0; }

/* The frontend calls this after a core restart; the core only needs the link type/MAC re-applied. */
static int r8_restore_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    if (rtl8188eu_br_add_station_if(g_r8.core)) return -EIO;
    (void)r8_set_channel_from_conf(hw);
    return 0;
}

/* Only the efuse MAC is supported; random MAC (AWDL / private address) needs REG_MACID writes in the core. */
static int r8_set_station_mac(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const uint8_t *mac)
{
    return memcmp(mac, g_r8.mac, ETH_ALEN) == 0 ? 0 : -EOPNOTSUPP;
}

static void r8_connect_hw_setup(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const uint8_t *bssid)
{
    (void)r8_set_channel_from_conf(hw);
    rtl8188eu_br_set_bssid(g_r8.core, bssid);
}

static void r8_restore_connected_hw(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const uint8_t *bssid)
{
    r8_connect_hw_setup(hw, vif, bssid);
}

static void r8_force_wifi_only(void) {}   /* no BT coex on this chip path */

static void r8_get_fw_version(struct rtw_dev *dev, uint16_t *version, uint8_t *sub)
{
    if (version) *version = 28;           /* rtl8188eufw.bin revision 28.0 (log: "revision 28.0") */
    if (sub) *sub = 0;
}

static void r8_get_chip_name(struct rtw_dev *dev, char *buf, size_t sz)
{
    static const char name[] = "RTL8188EU";
    size_t n = sizeof(name) < sz ? sizeof(name) : sz;
    if (!buf || !sz) return;
    memcpy(buf, name, n);
    buf[sz - 1] = '\0';
}

static void r8_get_stats(struct rtw_dev *dev, uint32_t *tx, uint32_t *rx)
{
    if (tx) *tx = 0;      /* byte counters not kept yet */
    if (rx) *rx = 0;
}

static uint8_t r8_get_tx_nss(struct rtw_dev *dev) { return 1; }
/* The frontend flow control was sized for the PCIe ring (stall below 96 free, resume at 160). The USB core has only 8 TX slots, so
 * raw counts would stall forever (v0.16.0 log: be_avail=8, stalled=1, qdrop=128). Scale by 32: stall at <3 free, resume at >=5 free. */
static uint32_t r8_tx_avail(void) { return g_r8.core ? rtl8188eu_br_tx_free_slots(g_r8.core) * 32u : 0; }
static int r8_tx_busy(void) { return g_r8.core && rtl8188eu_br_tx_free_slots(g_r8.core) < 8; }

static const struct rtw88_core_ops r8188eu_core_ops = {
    .name                           = "RTL8188EU/USB",
    .probe                          = r8_probe,
    .remove                         = r8_remove,
    .is_scanning                    = r8_is_scanning,
    .hw_scan_supported              = r8_hw_scan_supported,
    .sw_scan_start                  = r8_sw_scan_start,
    .sw_scan_switch_channel         = r8_sw_scan_switch_channel,
    .sw_scan_complete               = r8_sw_scan_complete,
    .restore_interface              = r8_restore_interface,
    .set_station_mac                = r8_set_station_mac,
    .connect_hw_setup               = r8_connect_hw_setup,
    .restore_connected_hw           = r8_restore_connected_hw,
    .restore_connected_hw_timeslice = r8_restore_connected_hw,
    .force_wifi_only                = r8_force_wifi_only,
    .get_fw_version                 = r8_get_fw_version,
    .get_chip_name                  = r8_get_chip_name,
    .get_stats                      = r8_get_stats,
    .get_tx_nss                     = r8_get_tx_nss,
    .tx_avail                       = r8_tx_avail,
    .tx_busy                        = r8_tx_busy,
};

/* Provider calls this (after rtl8188eu_hw_set_core) before starting the frontend; NULL restores PCIe behavior. */
void rtl8188eu_hw_register(bool on) { rtw88_set_core_ops(on ? &r8188eu_core_ops : NULL); }
