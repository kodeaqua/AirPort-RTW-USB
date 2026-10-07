# CLAUDE.md — AirPort-RTW USB support (RTL8188EU)

## Goal

Make a Realtek **USB** Wi-Fi dongle (TP-Link, **RTL8188EU**) work as a **native macOS Wi-Fi interface**
(menu bar, scan, join, WPA2) on a Hackintosh, by extending **AirPort-RTW**
(`github.com/JoMei9019-real/AirPort-RTW`).

Not the goal: a separate utility app, or the chris1111 "Wireless USB Big Sur Adapter" approach.
That package is a prebuilt binary with its own frontend, not IO80211. It is only a behavioral
reference, and it must NOT be installed while testing (it can match the same device).

## What is known (verified from READMEs)

- AirPort-RTW: native IO80211 driver, derived from the Feixiao rtw88 macOS port. Frontend is
  IO80211FamilyLegacy + IOSkywalkFamily. Hardware core is Linux-style rtw88.
- Currently **PCIe only**: RTL8821CE (10ec:c821, b821), RTL8822BE (b822), RTL8822CE (c822, c82f).
  RTL8812AE / 8814AE exist in the lineage but are disabled.
- Targets x86_64 Hackintosh, Ventura IO80211 ABI. Sonoma/Sequoia/Tahoe need the restored legacy
  Wi-Fi stack (IOSkywalkFamily + IO80211FamilyLegacy from OpenCore Legacy Patcher payloads).
  Monterey and earlier are not validated.
- Repo layout: `src/`, `ctl/`, `firmware/`, `scripts/` (`bootstrap-deps.sh`), `AirPortRTW.kext/Contents`,
  `Makefile`, `OPENCORE_GUIDE.txt`, `NOTICE.md`. Build: `./scripts/bootstrap-deps.sh && make airport`
  → `build/out/AirPortRTW.kext`.
- GPL-2.0. Keep SPDX/copyright headers. Update `NOTICE.md` for any code taken from other projects.
- `lwfinger/rtw88` (Linux backport) does **not** list RTL8188EU. Its USB chips are 8723DU, 8811AU,
  8811CU, 8812AU, 8812BU, 8812CU, 8814AU, 8821AU, 8821CU, 8822BU, 8822CU.
- Therefore the rtw88 core cannot drive the 8188EU. A different core is required.

## Progress (updated 2026-10-07)

Details: `docs/recon.md`, `docs/decisions.md`, `docs/stage2-probe.md`.

- **Stage 0 recon: done.** Frontend drives the core via mac80211 `hw->ops`, but is coupled to rtw88
  (`rtw_pci_probe`, chip table, ~25 `rtw88_*` compat helpers, single global `g_rtw88_hw`).
  PCI dependence in `AirPortRTW.cpp` is localized (start/interrupt/config/power); ioctl layer is shareable.
- **Baseline `make airport` builds OK** on macOS 26.7.1 (inside the `AirPort-RTW/` clone, which is
  gitignored; our own code lives in `src/usb/`, `kext/`, root `Makefile`).
- **Stage 1 core: `rtl8xxxu` proposed** (supports RTL8188EU, GPL-2.0-only, fw `rtlwifi/rtl8188eufw.bin`).
  Limitation: cut I unsupported.
- **Stage 2: PASS on hardware (2026-10-07, v0.1.1).** Attaches to interface 0; endpoints 0x81 bulk IN,
  0x02/0x03 bulk OUT; `REG_SYS_CFG` = 0x24403735, cut 3 (= D, supported). Verified against rtl8xxxu source.
- **Stage 3a (efuse): PASS on hardware (2026-10-07, v0.2.0).** 9346CR=0x20 boot=EFUSE; `rtl_id`=0x8129 OK;
  MAC 50:3d:d1:6d:14:6a (efuse @0xD7); efuse VID/PID da 0b 79 81 = 0bda:8179 matches ioreg. Efuse read took ~430 ms.
- **Stage 3a' (power_on): PASS on hardware (2026-10-07, v0.3.0).** Ported from rtl8xxxu 8188e.c; log:
  `power_on OK (0x00000000): CR=0x063f SYS_CLKR=0xfca3 (MAC_CLK on) APS_FSMCO=0x20020002`.
- **Stage 3b (firmware): PASS on hardware (2026-10-07, v0.4.1).** fw 15262 bytes, rev 28.0, sig 0x88e1;
  initQueues: 2 bulk OUT, no low queue; log: `firmware RUNNING (0x00000000): FW_DL=0x000300c6`.
  Next (3c): MAC/BB/RF init (`rtl8xxxu_init_device`, LLT, `request_hw_feature`) - verify against source first.
- **Stage 3c-1 (init_mac): PASS on hardware (2026-10-07, v0.5.0).** 92 MAC table writes + MAX_AGGR_NUM; log:
  `init_mac OK (0x00000000): MAX_AGGR_NUM=0x0707 0x428=0x0a 0x652=0x20` (all match expected).
- **Stage 3c-2 (RF accessor + init_phy_bb): PASS on hardware (2026-10-07, v0.6.0).** log: `init_phy_bb OK (0x00000000):
  0x800=0x80040000 0x804=0x00000003 0x808=0x0000fc00; rf_read(A,0x00)=0x33e73` (RF read works; value informational).
  **Stage 3c-3 (init_phy_rf): ran OK on hardware (v0.7.1), readback 36/49 match, 13 mismatch unexplained**
  (table and accessors verified identical to Linux; not a blocker, proof comes from RX). See `docs/stage3c-init.md`.
  3c-4 (WMAC/LLT/EDCA) and 3c-5 (tx power, LC cal, tail) v0.8.0 ran OK on hardware (init_wmac/init_tail; RFSW PAPE-shift bug found, fixed in v0.8.1, awaiting retest; HWSEQ reads 0x7f, unexplained).
  4a passive RX scan (v0.10.0): PASS on hardware (17 mgmt frames, crc_bad=0, AP heard on ch9-13 only; heard_on=1 is a stale-FIFO artifact, see docs/stage3c-init.md).
  **PASS on hardware (2026-10-07, one combined run):** v0.10.1 stale-FIFO drain (ch1-8 clean, AP heard on ch9-13 = normal +-2 ch leakage of a ch11 AP), v0.11.0 active scan + TX (tx_ok=1 tx_fail=0 per channel),
  v0.12.x link regs/CCMP CAM/H2C (HMTFR=0, SECCFG=0xcf), v0.13.0 async engine (rx 42/42, tx 5/5, errors=0, 18 beacons/2 s), init_wmac RFSW fix (v0.8.1) confirmed.
  BCN_PSR_RPT reads 0x0001 after writing 0xc001 (bits 15:14 do not read back; code matches Linux core.c:4930, expectation fixed in v0.13.1).
  **TX over the air PROVEN (2026-10-07, v0.13.1 run 2):** `probe_resp=11 to_us=11` (probe responses addressed to our MAC), beacons=19, rx 44/44 crc_bad=0 errors=0, tx 5/5 errors=0, link_selftest all expected values. Frontend shim (stage4-plan WP4-6) may start. See `docs/test-checklist.md`.
  Measured seam (2026-10-07): frontend needs ~45 `rtw88_*`/`rtw_pci_*` symbols; PCI coupling in RTW88IEEE80211.cpp is ~8 sites (L35-44, 482-530, 822-863, 933, 982),
  AirPortRTW.cpp has 43 PCI sites. The AirPort-RTW clone is gitignored here, so frontend changes must be kept as patch files in this repo.
  Data-frame txdesc: ported (2026-10-07, `src/usb/rtl8188eu_txdesc.h`, queue select, QoS, CCMP sec bits, rate param, RTS/short preamble; host test `make test-txdesc`; mgmt output byte-identical to the hardware-proven version; data frames NOT yet tested on hardware).
  Deferred: phy_iq_calibrate, set_crystal_cap, 40 MHz, RSSI/phystats, TX report, A-MPDU/HT40/SGI txdesc bits, driver-side rate adaptation (ra_info; data rate is a caller-supplied parameter, GUESS: conservative basic rate until RA exists).
- Old `RTW88USBDevice.cpp` in the clone is stale/unbuilt; do not build on it.

## Verified facts (from this session)

- User dongle: Realtek `0bda:8179` "802.11n NIC" (`ioreg -p IOUSB`; `system_profiler` printed nothing).
  Interface 0, class 0xFF, 3 endpoints. USB 2.0.
- macOS **26.7.1 Tahoe** (needs restored legacy IO80211 stack for the final Wi-Fi driver).
- Tahoe IOUSBHostFamily = 1.2 (compat 1.0.1). Match on `IOUSBHostInterface`; interface
  `deviceRequest()` has no `forClient` arg.
- Register access (rtl8xxxu): ctrl req 0x05, type 0xC0 read / 0x40 write, wValue=addr, wIndex=0, 500 ms.
  `REG_SYS_CFG`=0xF0, cut=(v&0xF000)>>12, cut letter = 'A'+cut, cut 8 (I) rejected.
- Linux order: identify_chip -> read_efuse -> parse_efuse -> (later) power_on/firmware. Efuse is read BEFORE power_on.
- Logs: use `/usr/bin/log show` (zsh shadows `log`); `dmesg` shows nothing. `make airport` runs inside `AirPort-RTW/`.

## Working conventions

- Commit messages in English, conventional prefixes `feat:`, `fix:`, `chore:` only.
- Chat with the user in Indonesian.

## Unverified — check before relying on it (items marked ✔ are resolved above)

- ✔ Whether upstream Linux `rtl8xxxu` covers RTL8188EU in current trees (my assumption: partially or not;
  RTL8188EU historically had out-of-tree `r8188eu` / `rtl8188eus`). **Verify first.**
- ✔ Exact USB VID:PID of the user's TP-Link dongle (get via `system_profiler SPUSBDataType` on macOS).
- Whether AirPort-RTW's frontend talks to the core through a narrow interface (ops table) or is
  entangled with rtw88 internals. This decides the whole design.
- Required firmware file for 8188EU (`rtl8188eufw.bin` is the expected name) and its license/redistribution terms.
- Target macOS version of the user's machine.

## Plan (do in order, one stage per reboot-test)

0. **Recon (no code):** read `src/` and map where the PCIe transport attaches to the rtw88 core,
   and where the IO80211 frontend calls into the core. Write findings to `docs/recon.md`.
   Identify the seam where a second core + USB transport can plug in.
1. **Pick the 8188EU core.** Candidates: Linux `rtl8xxxu` (mac80211-style, closest to the existing
   frontend contract) vs vendor `rtl8188eus` (self-contained HAL, messier). Decide with evidence
   from stage 0, and record the decision in `docs/decisions.md`.
2. **USB transport on macOS (IOUSBHost):** match by VID:PID, claim interface, find bulk IN/OUT
   endpoints, implement register read/write via vendor control requests, async bulk TX/RX with
   completions, safe teardown on hot-unplug.
3. **Core bring-up:** chip/version read, efuse (MAC address), firmware download, MAC/BB/RF init.
4. **Frontend hookup:** register the 8188EU core behind the same interface used by rtw88 so
   scan/assoc/TX/RX paths in AirPort-RTW are reused. 2.4 GHz, 1T1R, 11n only.
5. **Hardening:** hot-unplug, sleep/wake, reconnect, no conflict with the PCIe path.

Each stage needs a defined pass/fail test (see "Test protocol") before moving on.

## Test protocol

Order of validation, one step per reboot:
1. kext loads and attaches to the USB device (IORegistry shows it)
2. register read returns sane chip ID; MAC from efuse
3. firmware download succeeds
4. scan shows nearby 2.4 GHz networks
5. join WPA2 network, ping works
6. sleep/wake, unplug/replug

On any failure, capture: `log show --last 5m --predicate 'sender == "AirPortRTW"'`,
`kextstat | grep -i rtw`, and the panic report from `/Library/Logs/DiagnosticReports/` if any.
Do not iterate blindly; read the log first.

## Rules for working in this repo

- This is **kernel code**. A bug can panic the machine. Prefer small, reviewable changes.
- Always keep a bootable fallback EFI. Never tell the user to replace their only working EFI.
- Do not load the old Feixiao `rtw88.kext` together with AirPortRTW (both match the same PCI device).
  Do not load the chris1111 USB package while testing the new USB path.
- Do not invent register values, USB IDs, firmware names, or API behavior. If unsure, say so and
  cite the file/line or source you are working from. Mark guesses as guesses in code comments.
- Do not commit firmware blobs unless their license allows it; fetch them in `scripts/` like the
  existing bootstrap does.
- Keep the PCIe path working. Run the existing build (`make airport`) after every change and
  confirm it still compiles before touching hardware-facing behavior.
- Match the existing code style and file organization. Put new USB/8188EU code in clearly named
  files (e.g. `src/usb/`, `src/rtl8188eu/`) rather than editing rtw88 internals in place.
- Add a short note to `RELEASE_NOTES` / docs for any user-visible change, including the new
  supported device IDs and the OpenCore steps they need.

## Collaboration notes

- The user builds and tests on their own Hackintosh and reports logs back. Claude Code should make
  every change easy to test: say exactly what to build, what to copy, what to expect, and what to
  send back if it fails.
- When a stage is blocked on missing information (e.g. macOS version, dongle PID), ask for exactly
  that item rather than proceeding on assumptions.
