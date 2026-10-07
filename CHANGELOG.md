# Changelog

Versions are the standalone probe/core version (`RTL8188EUCore`). "HW" = verified on the author's dongle (0bda:8179, cut D).

## 0.21.0 (not yet run on hardware)
- HT20 (11n, 1 stream, MCS0-7, long GI) advertised to the frontend; AP rate set and HT MCS mask feed the rate adaptation through
  a port of `rtl8xxxu_refresh_rate_mask` (mask by signal level, re-evaluated every 2 s). EAPOL frames always go out at 6M.
  No TX A-MPDU aggregation, no 40 MHz, no SGI yet. Host test extended.
## 0.20.0 (not yet run on hardware)
- Software rate adaptation ported from rtl8xxxu `8188e.c` (TX report type 2, rate decision, power-training state), `src/usb/rtl8188eu_ra.h`,
  tables checked number-for-number against Linux; host test `make test-ra`. Legacy rates only.
## 0.19.0 (not yet run on hardware)
- Unicast data TX rate chosen from smoothed RX RSSI instead of a fixed 6M.
## 0.18.0 (not yet run on hardware)
- USB sleep/wake (clear pipe halts, register check, full re-init) and hot-unplug (`markGone`) for the integrated driver (patch 0003).
- Diagnostics for duplicate ping replies: `rx DUPSEQ`, `tx ICMP`.
## 0.17.0 - 0.16.1
- Per-frame RSSI from PHY status so macOS shows real signal bars. HW: bars full.
- RXFLTMAP2 accepts all data subtypes; `tx_avail` scaled so frontend flow control does not stall on 8 USB TX slots. HW: join WPA2 + ping works.
## 0.16.0 - 0.15.0
- Diagnostics to find the missing EAPOL M1, register access serialized with a recursive lock, RX stall monitor, TX wedge dump.
## 0.14.0 - 0.13.0 (HW)
- Data-frame TX descriptor, async bulk RX/TX engine, TX over the air proven (probe responses addressed to our MAC).
## 0.12.x - 0.11.0 (HW)
- Link-layer registers, CCMP CAM keys, H2C media status; active scan with TX.
## 0.10.x - 0.9.0 (HW)
- Passive RX scan, 20 MHz channel switch.
## 0.8.x - 0.5.0 (HW)
- WMAC/LLT init, TX power, LC calibration, RF/BB/MAC init tables.
## 0.4.x - 0.1.1 (HW)
- Firmware download, power-on, efuse (MAC), USB attach.
