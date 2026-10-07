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
