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

## v0.18.0: diagnostics + sleep/wake + hot-unplug (patch 0003, NOT yet run on hardware)

Changes: `rx DUPSEQ` / `tx ICMP` log lines (duplicate-reply diagnosis); `RTL8188EUCore::markGone()` (called from
`AirPortRTW::willTerminate`, all later I/O returns NoDevice); wake in USB mode (`restoreAfterSystemWake`) runs
`resumeCheck()` (clearStall on all pipes + SYS_CFG read) then forces the full init on the next `start()`.
GUESS: whether the USB port loses power in S3 is unknown, so a full re-init is always done.

Test A (hot-unplug): join WPA2, unplug the dongle. Expect `USB provider terminating (hot-unplug)` and `device gone`, no panic.
Replug: the interface should reappear and join again. Send `sudo dmesg | grep -E "AirPortRTW|RTL8188EU" | tail -60`.
Test B (sleep/wake): join WPA2, sleep 30 s, wake. Expect `resume check: SYS_CFG read -> 0x00000000 (0x24403735)`,
`wake complete`, then CoreWiFi rejoins. If `PM transition ... failed` appears, send the lines around it.
Test C (duplicates): `ping -c 30 8.8.8.8`, then `sudo dmesg | grep -E "rx DUPSEQ|tx ICMP"`.

## v0.19.0: RSSI-based TX rate (NOT yet run on hardware)

Unicast data frames now use a rate from the smoothed RX RSSI (`pickDataRate`): >=-58 dBm 54M, >=-64 36M, >=-70 24M, >=-76 12M,
else/unknown/group 6M. GUESS thresholds, no feedback loop. Test: `ping -c 30 <gateway>` and a speed test; the `stats:` line now
shows `rssi=`. If loss or retries get worse than v0.18.0 (6M), tell me and the thresholds go down; revert = one line in `RTL8188EUBridge.cpp`.

## v0.20.0: software rate adaptation from TX reports (NOT yet run on hardware)

`src/usb/rtl8188eu_ra.h` ports rtl8xxxu 8188e.c RA (tables verified number-for-number against Linux master, host test `make test-ra`).
Flow: first unicast RSSI sample -> RA starts at the v0.19.0 RSSI-map rate -> TX reports (rpt_sel=2, already enabled in init_tail) feed
`handleItem()` -> `decision_rate` goes into the txdesc, `pt_stage` too. `REG_TX_REPORT_TIME` updates are queued and written from the TX path.
Legacy rates only (mask 0x0fff). RA resets on join/leave. GUESS: start rate; whether reports really arrive every ~200 ms is unverified.
Look for in dmesg: `ra: init start rate idx N`, `ra: rate idx A -> B (retry ...)`. If you never see a rate change during a long
transfer, or the rate sticks at 1M, send `sudo dmesg | grep -E "ra:|stats:"`.
