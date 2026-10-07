# Stage 5 test: AirPortRTW with the RTL8188EU USB path (first integrated boot)

Status 2026-10-07: builds and links (`make airport-usb`); never run. Expect the first integrated boot to fail somewhere; one log tells which layer.

## What it is
One kext (`AirPortRTW`) that now also matches `IOUSBHostInterface` 0bda:8179 (personality `AirPortRTW_8188EU_USB`). With a USB provider it:
creates `RTL8188EUCore` (`rtl8188eu_core_create`), registers the `rtw88_core_ops` table, and runs the normal frontend `start()`.
Core ops `probe` runs the full hardware init (efuse -> firmware -> MAC/BB/RF -> WMAC -> tail, ~1-2 s) and publishes an `ieee80211_hw`
whose ops call the core. No PCI/MMIO/IRQ is touched (`_isUsb`). Patches: `patches/0001-core-ops.patch`, `patches/0002-usb-provider.patch`
(applied to the gitignored clone by `scripts/apply-usb-patches.sh`).

## Build
    make usbprobe          # once: fetches firmware, generates tables into build/fw
    make airport-usb       # -> AirPort-RTW/build/out/AirPortRTW.kext (PCIe-only: `make -C AirPort-RTW airport`, unchanged)

## Before loading
- Unload/uninstall the probe: `make uninstall` (RTL8188EUProbe matches the same device; both must NOT be loaded). Do not install chris1111's package.
- Tahoe needs the restored legacy IO80211FamilyLegacy + IOSkywalkFamily (see AirPort-RTW/OPENCORE_GUIDE.txt); without them the kext cannot load.
- Keep a bootable fallback EFI. Inject via OpenCore (Kernel > Add) like the PCIe build; MinKernel 22.0.0.

## Pass / fail
1. `kmutil showloaded | grep -i AirPortRTW`, `ioreg -p IOService -w0 | grep -i AirPortRTW` shows the controller under the USB interface.
2. Log (`/usr/bin/log show --last 5m --predicate 'sender == "AirPortRTW"' --info`): `USB RTL8188EU core attached`, `initHardware OK, MAC 50:3d:d1:6d:14:6a`,
   `core ops probe (RTL8188EU/USB)`, `IEEE80211::start complete - SUCCESS`, `controller and network interface registered`.
3. A Wi-Fi interface appears (System Settings > Network / menu bar); scan lists "Rumah 4G" (ch11, WPA2 CCMP expected).
4. Join WPA2 + ping. TKIP/WEP networks cannot be joined (set_key returns -EOPNOTSUPP).
5. Later: sleep/wake, unplug/replug (not handled yet: no hot-unplug path in AirPortRTW; unplug while loaded is untested).

## If it fails, send back
`log show` output above (whole `AirPortRTW` + `kernel` lines for 2 minutes), `kmutil showloaded | grep -iE "rtw|8188"`, and any panic report
from /Library/Logs/DiagnosticReports/. Stage markers `rtw88: ... RTW88_STAGE` show how far `IEEE80211::start()` got.

## Known gaps (by design in this slice)
Legacy rates only (no HT/40 MHz), fixed 6M data rate (GUESS), no RSSI/TX status, efuse MAC only, TKIP/WEP unsupported, sleep/wake and hot-unplug not adapted
(PM paths still assume PCI: wake returns NotReady for USB), RX handles one packet per USB buffer.

## Run 1 findings (2026-10-07, v0.15.4, `rtw-full.txt`)
- Integrated kext loads, `en1` (AirPortRTWInterface) exists; RTL8188EUProbe was NOT loaded (the `RTL8188EUProbe:` log prefix is the core's `LOGP`).
- TX healthy: 2560 submissions, 1 in flight, err=0 (no bulk OUT timeout, `tx_wedge` never fired).
- RX 802.11 frames stop after ~200 s (frames flat at 416) while only C2H keeps arriving; RCR/MSR unchanged, BSSID still 0, `visible=0`.
  Scans hop through ch11 meanwhile, so the radio is deaf, not merely parked on an empty channel.
- Audit finding: all register access shared one control buffer with no lock while the frontend calls in from several threads
  (scan, TX, bss_info, AWDL) -> interleaved RF/BB/RCR writes. Fixed in v0.15.5 (recursive `_lock`, `CoreLock`).
- v0.15.5 also logs every channel change (first 40), prints `ch=` in `stats`, and on an RX stall dumps CR/SYS_FUNC_EN/RF18/BB800/BB900/BCN_CTRL/MSR
  (`rx_stall:` line) then re-applies the channel as a recovery experiment. Send back all `RTL8188EUProbe:` lines (`sudo dmesg`).
