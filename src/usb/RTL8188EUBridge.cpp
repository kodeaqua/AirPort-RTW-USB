// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUBridge.cpp - C entry points over RTL8188EUCore (see rtl8188eu_bridge.h).
#include "RTL8188EUCore.hpp"
#include "rtl8188eu_bridge.h"

#define C(core) ((RTL8188EUCore *)(core))

extern "C" {

bool rtl8188eu_is_usb_provider(void *provider) { return OSDynamicCast(IOUSBHostInterface, (IOService *)provider) != nullptr; }

void *rtl8188eu_core_create(void *owner, void *provider)
{
    RTL8188EUCore *c = OSTypeAlloc(RTL8188EUCore);
    if (!c) return nullptr;
    if (!c->init() || !c->attach((IOService *)owner, OSDynamicCast(IOUSBHostInterface, (IOService *)provider))) {
        c->release();
        return nullptr;
    }
    return c;
}

void rtl8188eu_core_destroy(void *core)
{
    if (!core) return;
    C(core)->closeAll();
    C(core)->release();
}

int rtl8188eu_br_init_hw(void *core) { return C(core)->initHardware(); }

void rtl8188eu_br_get_mac(void *core, uint8_t mac[6]) { C(core)->getMac(mac); }

int rtl8188eu_br_async_start(void *core, rtl8188eu_rx_fn rx, rtl8188eu_txdone_fn txdone, void *ctx)
{
    RTL8188EUCore *c = C(core);
    IOReturn r = c->asyncStart();
    if (r != kIOReturnSuccess) return r;
    c->_cbCtx = ctx;
    c->_rxCb = nullptr;
    c->_rxCbEx = (RTL8188EUCore::RxCallbackEx)rx;
    c->_txDoneCb = (RTL8188EUCore::TxDoneCallback)(void *)txdone;   // (ctx, cookie, status): IOReturn is int-sized
    r = c->rxStart();
    if (r != kIOReturnSuccess) c->asyncStop();
    return r;
}

void rtl8188eu_br_async_stop(void *core) { C(core)->asyncStop(); }

int rtl8188eu_br_set_channel(void *core, int ch)              { return C(core)->setChannel(ch); }
int rtl8188eu_br_add_station_if(void *core)                   { return C(core)->addStationInterface(); }
int rtl8188eu_br_set_bssid(void *core, const uint8_t *b)      { return C(core)->setBssid(b); }
int rtl8188eu_br_set_basic_rates(void *core, uint32_t r)      { return C(core)->setBasicRates(r); }
int rtl8188eu_br_set_short_preamble(void *core, bool on)      { return C(core)->setShortPreamble(on); }
int rtl8188eu_br_set_slot(void *core, bool s, bool ht)        { return C(core)->setSlot(s, ht); }
int rtl8188eu_br_join(void *core, uint16_t aid)               { return C(core)->joinBss(aid); }
int rtl8188eu_br_leave(void *core)                            { return C(core)->leaveBss(); }
int rtl8188eu_br_set_key_ccmp(void *core, uint8_t idx, bool pw, const uint8_t *mac, const uint8_t *k, uint8_t *hw)
                                                              { return C(core)->setKeyCcmp(idx, pw, mac, k, hw); }
int rtl8188eu_br_clear_key(void *core, uint8_t hw)            { return C(core)->clearKey(hw); }

int rtl8188eu_br_tx(void *core, const uint8_t *frame, uint16_t len, const rtl8188eu_br_txparams *p, void *cookie)
{
    rtl8188eu_tx::Params q = {};
    q.queue = rtl8188eu_tx::selectQueue(frame, p->ac);
    // GUESS: fixed 6M OFDM for data until driver-side rate adaptation exists (mgmt is always 1M in the builder).
    q.rate = rtl8188eu_tx::kRate6M;
    q.sec = p->ccmp ? rtl8188eu_tx::kSecAes : rtl8188eu_tx::kSecNone;
    q.shortPreamble = p->short_preamble;
    q.useRts = p->use_rts;
    q.useCtsSelf = p->use_cts_self;
    q.macidKeyIdx = p->hw_key_idx;
    q.seqOverride = -1;
    return C(core)->txSubmitFrame(frame, len, q, cookie);
}

unsigned rtl8188eu_br_tx_free_slots(void *core)
{
    return C(core)->txFreeSlots();
}

}
