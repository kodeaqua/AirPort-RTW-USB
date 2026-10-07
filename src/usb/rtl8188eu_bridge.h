/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rtl8188eu_bridge.h - plain-C view of RTL8188EUCore for the mac80211-shaped face (RTL8188EUHw.cpp).
 * The face is compiled against the AirPort-RTW compat headers, which must not meet the IOUSBHost headers
 * that RTL8188EUCore.hpp pulls in, so the two sit in separate translation units and talk through this.
 * `core` is an RTL8188EUCore*. Return values are IOReturn (0 = success) unless noted.
 */
#ifndef RTL8188EU_BRIDGE_H
#define RTL8188EU_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rtl8188eu_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len, bool decrypted, bool has_signal, int8_t signal_dbm);
typedef void (*rtl8188eu_txdone_fn)(void *ctx, void *cookie, int status);

/* Provider side (implemented in RTL8188EUBridge.cpp; void* = IOService* / RTL8188EUCore*). */
bool  rtl8188eu_is_usb_provider(void *provider);
void *rtl8188eu_core_create(void *owner, void *provider);   /* alloc + attach (opens the interface); NULL on failure */
void  rtl8188eu_core_destroy(void *core);                   /* closeAll + release; idempotent per core */

/* Provider -> face: which core the next rtw88_core_ops.probe() should drive (set before IEEE80211::start()). */
void  rtl8188eu_hw_set_core(void *core);
void  rtl8188eu_hw_register(bool on);                    /* install/remove the rtw88_core_ops table */

int   rtl8188eu_br_init_hw(void *core);                  /* full init chain, idempotent per attach */
void  rtl8188eu_br_get_mac(void *core, uint8_t mac[6]);
int   rtl8188eu_br_async_start(void *core, rtl8188eu_rx_fn rx, rtl8188eu_txdone_fn txdone, void *ctx);
void  rtl8188eu_br_async_stop(void *core);
int   rtl8188eu_br_set_channel(void *core, int channel);
int   rtl8188eu_br_add_station_if(void *core);
int   rtl8188eu_br_set_bssid(void *core, const uint8_t bssid[6]);
int   rtl8188eu_br_set_basic_rates(void *core, uint32_t rate_cfg);     /* bit0 = 1M ... bit11 = 54M */
int   rtl8188eu_br_set_short_preamble(void *core, bool on);
int   rtl8188eu_br_set_slot(void *core, bool short_slot, bool peer_ht);
int   rtl8188eu_br_join(void *core, uint16_t aid);
int   rtl8188eu_br_leave(void *core);
int   rtl8188eu_br_set_key_ccmp(void *core, uint8_t keyidx, bool pairwise, const uint8_t *mac, const uint8_t *key16, uint8_t *hw_idx);
int   rtl8188eu_br_clear_key(void *core, uint8_t hw_idx);

struct rtl8188eu_br_txparams {
    uint8_t ac;              /* Linux IEEE80211_AC_* (VO=0 VI=1 BE=2 BK=3) */
    bool    ccmp;            /* hw_key present: set SEC_AES */
    bool    short_preamble;
    bool    use_rts;
    bool    use_cts_self;
    uint8_t hw_key_idx;      /* 0xff = none */
};
int   rtl8188eu_br_tx(void *core, const uint8_t *frame, uint16_t len, const struct rtl8188eu_br_txparams *p, void *cookie);
unsigned rtl8188eu_br_tx_free_slots(void *core);

#ifdef __cplusplus
}
#endif
#endif
