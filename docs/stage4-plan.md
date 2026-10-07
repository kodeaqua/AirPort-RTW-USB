# Stage 4 plan: native Wi-Fi (on/off, scan, connect) on the 8188EU core

Written 2026-10-07. Evidence: docs/recon.md; frontend calls 13 mac80211 ops (counted in RTW88IEEE80211.cpp):
tx(9 call sites), cancel_hw_scan, start, stop, set_key, bss_info_changed, configure_filter, hw_scan, sta_add/remove,
add/remove_interface, config; plus ~42 `rtw88_*` compat-helper calls. Linux rtl8xxxu is core.c 8295 + 8188e.c 1886 lines.

## Approach
Keep the hand-ported C++ core (it is the only part verified on hardware, stages 2-3c) and give it a mac80211-shaped
face (`ieee80211_ops`) that the existing RTW88IEEE80211 state machine drives. Do NOT compile rtl8xxxu core.c wholesale:
it needs real USB/URB/workqueue/skb compat that does not exist yet.

## Work packages (each must compile; each logs its own result; hardware test per WP is what the user wants to skip)
1. RX async + frame delivery (rxScan is a synchronous stub; need continuous bulk IN with completions, rxdesc16 parse, phystats/RSSI).
2. TX path: txdesc32 for 8188e (rtl8xxxu_fill_txdesc_v1 / v2 check), bulk OUT queue selection, mgmt/data/beacon-less TX, TX report.
3. Filters/keys: configure_filter (RCR), set_key via CAM (WPA2 CCMP), sta_add/remove, bss_info_changed (BSSID, basic rates, preamble, slot).
4. Per-core abstraction in RTW88IEEE80211: replace rtw_pci_probe/chip table and ~42 `rtw88_*` helpers with a vtable; kill single `g_rtw88_hw`.
5. USB provider class: IO80211Controller on IOUSBHostInterface (start/stop/power; USB completions instead of PCI IRQ), Info.plist USB personality.
6. Integration build target (`make airport` must keep passing), install/OpenCore notes in RELEASE_NOTES.

## Honest risk statement
Nothing in WP1-6 can be verified without hardware. WP4/5 touch 8000+ lines of frontend code not written for this device.
Expect the first integrated boot to fail somewhere; a single log tells which layer. Keep AirPortRTW PCIe path building throughout.

## Status 2026-10-07 (after v0.13.0)
Done in the probe kext (compiles, untested on hardware): WP1 async RX engine (no RSSI yet), WP2 mgmt-frame TX + async TX pool (no data/QoS/rate-adaptation yet),
WP3 link regs / CCMP CAM / H2C media status (no configure_filter, no sta_add macid allocation). WP4-6 not started: they depend on TX/RX being proven (docs/test-checklist.md).
Measured for WP4: ~45 undefined `rtw88_*`/`rtw_pci_*` symbols from the frontend objects; PCI sites in RTW88IEEE80211.cpp ~8, AirPortRTW.cpp 43.
Proposed WP4 shape: a small `RTW88CoreOps` hook (patch file in this repo, applied to the gitignored clone) replacing the rtw_pci_probe/chip-table block in `start()`.

## Status 2026-10-07 (WP4 step 1: core ops hook)
`patches/0001-core-ops.patch` (apply in `AirPort-RTW/`: `git apply ../patches/0001-core-ops.patch`) adds `src/compat/rtw88_core_ops.h`
(`struct rtw88_core_ops`, `rtw88_set_core_ops()`), dispatches 17 `rtw88_*` helpers in `rtw88_compat.c` through it when a table is registered,
and routes probe/remove in `RTW88IEEE80211::start()/stop()`. With no table registered the original PCIe code runs unchanged.
`make airport` passes with the patch applied. Not run on hardware (PCIe hardware not available here). Next: WP4 step 2, USB core ops
implementation + `ieee80211_hw/ops` face over the probe core (src/usb/), then WP5 provider (frontend `create()` still takes PCI types).

## Status 2026-10-07 (data-frame txdesc)
`src/usb/rtl8188eu_txdesc.h` is a pure builder ported from rtl8xxxu `rtl8xxxu_tx` + `fill_txdesc_v3` (queue select, pipe map for 2/3 bulk OUT eps,
QoS, SEC_AES/RC4, short preamble, RTS/CTS-self, pt_stage 5, fallback 0x1ff00, csum). `txSubmitFrame()` in the probe kext uses it; `txSubmitMgmt()` is a wrapper.
Verified on host: mgmt descriptors are byte-identical to the previous inline code (hardware-proven); data cases checked by hand against the Linux source.
Not ported: A-MPDU, HT40, SGI, TX report and rate adaptation (Linux data rate = ra_info.decision_rate, initial MCS7, adjusted from C2H reports).
Hardware validation idea (not written yet): in async_selftest, send a data frame to a beacon-learned BSSID while unassociated; a deauth/disassoc reply to our MAC proves the data txdesc (class-3 frame response).

## Status 2026-10-07 (core/probe split)
`RTL8188EUProbe` was split: `src/usb/RTL8188EUCore.{hpp,cpp}` is now an `OSObject` that owns everything hardware-related
(`attach(owner, iface)`, `bringUp()`, `closeAll()`, plus all register/TX/RX/link methods, now public), and `RTL8188EUProbe` is a thin IOService
(`start` = alloc core, attach, bringUp, registerService). Log prefix `RTL8188EUProbe:` and behavior are unchanged, so the pending v0.14.0 run also
confirms the refactor did not regress anything (init chain, `rx_scan`, `link_selftest`, `async_selftest` lines must be as in run 2).
Purpose: the Wi-Fi frontend (WP5 provider) will own an `RTL8188EUCore` directly and expose it through `rtw88_core_ops` + an `ieee80211_hw`.
