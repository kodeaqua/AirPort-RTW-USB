# Stage 3c — MAC/BB/RF init (RTL8188EU)

Source: torvalds/linux master, `drivers/net/wireless/realtek/rtl8xxxu/` (`core.c` 8295 lines, `8188e.c` 1886 lines),
fetched 2026-10-07. Line numbers refer to that fetch. Status: **recon only, nothing ported yet.**

## Order in `rtl8xxxu_init_device` (core.c:3958)
Already done in the probe kext (v0.4.1): power_on, queue reserved page, queue priority, TRXFF_BNDY+2 = 0x25ff,
firmware download + start. `hw_feature_report = 0` for 8188eu, so request/dump hw feature are no-ops
(`max_bw = 40`).

Remaining, in source order:
1. `init_mac` (core.c:2187): write `rtl8188e_mac_init_table` (8188e.c:19) as 8-bit writes, then
   `REG_MAX_AGGR_NUM` (16-bit) = 0x0707 for RTL8188E.
2. `init_phy_bb` (core.c:2310) -> `rtl8188eu_init_phy_bb` (8188e.c:582) with `rtl8188eu_phy_init_table`
   (8188e.c:46) and `rtl8188e_agc_table` (8188e.c:146). 32-bit writes with 1 us delay (`rtl8xxxu_init_phy_regs`).
3. `init_phy_rf` -> `rtl8188eu_init_phy_rf` (8188e.c:605) with `rtl8188eu_radioa_init_table` (8188e.c:215).
4. FPGA0 RF switch control, TX buffer boundary writes (only if MAC was not already powered), `REG_PBP` (128/128).
5. `llt_init` = `rtl8xxxu_init_llt_table` (core.c:2519): total_page_num 0xa9, `last_llt_entry` 175.
6. `usb_quirks` (8188e.c:1290), TX report enable, `REG_RX_DRVINFO_SZ` = 4, HISR/HIMR (8188E branch),
   `USB_SPEC_INT_BULK_SELECT`, RCR, RXFLTMAP/MAR, response rate, SIFS/retry, EDCA, DARFRC/RARFRC, ACKTO,
   beacon params, `init_aggregation` (8188e.c:524), packet lifetime, CCK+OFDM enable, CAM invalidate.
7. `set_tx_power` (`rtl8188f_set_tx_power`; reads efuse power tables), LEDCFG2 DPDT, HWSEQ_CTRL, BAR_MODE_CTRL,
   GPIO_MUXCFG, `phy_lc_calibrate` (core.c:3498), `phy_iq_calibrate` (8188e.c:906), thermal meter,
   NAV_UPPER, USB_HRPWM, FWHW_TXQ_CTRL, CFO tracking init.

## Findings that matter for the port
- **Order differs from what v0.4.x does.** In Linux, `init_queue_reserved_page`/`init_queue_priority`/TRXFF_BNDY
  come before firmware (we already do this), but LLT init comes AFTER BB/RF init and the TX-buffer-boundary writes.
  Do not "fix" this by moving LLT earlier without evidence.
- Tables are large (BB ~100 entries, AGC ~70, RF radio A ~300). These must be copied mechanically from the
  source, never retyped. Generate a header from the fetched file with a script in `scripts/`, and keep
  GPL-2.0 attribution in `NOTICE.md`.
- Steps 5-7 need RF register access (`rtl8xxxu_read_rfreg`/`write_rfreg`, HSSI serial interface) that the
  probe kext does not have yet. Verify its implementation in core.c before porting.
- `phy_iq_calibrate` (8188e.c:906-1262, ~350 lines) is the biggest single function; it is not needed for scan to
  start, so it can be deferred if it blocks.
- Several steps depend on efuse-derived values (`default_crystal_cap`, tx power tables, `tx_paths`/`rx_paths`).
  Stage 3a only parsed MAC/ID, so a `parse_efuse` port (8188e.c) is a prerequisite for `set_tx_power`.

## Proposed split (one reboot-test each)
- **3c-1:** `init_mac` (MAC table + MAX_AGGR_NUM). Pass = all writes OK, `REG_CR` still sane. Lowest risk.
- **3c-2:** RF register accessor + `init_phy_bb`. Pass = no write errors; read back a few BB regs.
- **3c-3:** `init_phy_rf` (radio A table). Pass = RF reg readback matches written values.
- **3c-4:** remaining WMAC/EDCA/beacon/aggregation/LLT/quirks block (step 4-6).
- **3c-5:** efuse tx power parse + `set_tx_power` + LC/IQ calibration.

Register values in every sub-step must be copied from source; mark any deviation as a guess in comments.

## 3c-3 result (2026-10-07, v0.7.1)
- `init_phy_rf` ran on hardware with no USB errors. Diagnostic readback (not in Linux): 49 regs checked, 36 match,
  13 mismatch (0x2f, 0x42, 0x83, 0xc4, 0xc6, 0xca, 0x51, 0x56, 0xb6, 0x19, 0x18, 0x1e, 0x1f).
- Checked against source: generated `rtl8188eu_radioa_init_table` is identical to 8188e.c (96 entries); `rfRead`/`rfWrite`
  match `rtl8xxxu_read_rfreg`/`write_rfreg` step by step. So the mismatch is not a copy error.
- Reg 0x2f read 0x14140 (v0.7.0) vs 0x101c0 (v0.7.1) for the same written value, so at least that register is not
  stable on readback. Cause unknown (guess: status/calibration bits, or readback not meaningful for these regs).
- Verdict: **ran OK, readback criterion not met, unexplained.** Not treated as a blocker; real proof is RF function
  (beacon RX during scan). Revisit if RX shows nothing.

## 3c-4 + 3c-5 (v0.8.0, written 2026-10-07, awaiting hardware test)
Ported from `build/src/core.c` (init_device 3958+), `8188e.c`, `8188f.c`; all constants checked in `regs.h`/`rtl8xxxu.h`.
- `initWmac()` (3c-4): RFSW control, TX buffer boundary 0xaa, PBP, LLT (0xa9 pages, last entry 175), usb_quirks,
  TX report, RX_DRVINFO_SZ=4, HISR/HIMR (8188E branch), RCR + MAR, response rate, SIFS/retry, EDCA, DARFRC/RARFRC,
  ACKTO, beacon params, init_aggregation, packet lifetime, CCK+OFDM enable, CAM invalidate.
- `setTxPower()` + `phyLcCalibrate()` + `initTail()` (3c-5): `rtl8188f_set_tx_power` (channel 1, from efuse cck_base @0x10 /
  ht40_base @0x16; diff terms are 0 for 8188EU), LEDCFG2 DPDT, HWSEQ=0xff, BAR_MODE_CTRL, GPIO_MUXCFG, LC calibration,
  thermal meter (RF 0x42 = 0x37cf8), NAV_UPPER, USB_HRPWM, FWHW_TXQ_CTRL.
- Deviation (guess): Linux skips the `!macpower` blocks if the MAC was already powered before power_on; we always take
  the full path since the probe always runs power_on first.
- NOT done (deferred): `phy_iq_calibrate` (8188e.c:906), `set_crystal_cap`/CFO tracking, rate-control init, channel switch.
- Expected log lines: `init_wmac OK` (RCR=0x7000600e, RX_DRVINFO=4, TRXFF_BNDY=0xaa, RFSW=0x07000760, SIFS_CCK=0x100a,
  EDCA_BE=0x005ea42b) and `init_tail OK` (NAV_UPPER=0xeb, HWSEQ=0xff, RF18 bit15 clear after LC calibration).
- Possible explanation for the 3c-3 mismatch at RF reg 0x18 (0x0f407 written, 0x07407 read): bit 15 is the LC
  calibration start bit, which hardware clears when done. Guess; the 3c-5 log (RF18 after LC cal) will tell.


## 3c-4 + 3c-5 hardware result (v0.8.0, 2026-10-07)
- `init_wmac OK`: CR=0x06ff, RCR, RX_DRVINFO, TRXFF_BNDY, SIFS_CCK, EDCA_BE all as expected.
- `init_tail OK`: NAV_UPPER=0xeb, RF18=0x07407 (bit 15 clear, so the LC-cal start bit self-clears: explains the 3c-3 RF18 mismatch),
  tx power ch1 cck=0x27 ofdm=0x2b from efuse.
- **RFSW mismatch (0x03000760 vs 0x07000760) = real bug in our port**, fixed in v0.8.1: Linux (core.c:4058-4061) also ORs
  `FPGA0_RF_PAPE << FPGA0_RF_BD_CTRL_SHIFT` (bit 26); we only set the unshifted PAPE.
- **HWSEQ_CTRL (0x423) wrote 0xff, read 0x7f: unexplained.** Same as Linux write; guess: bit 7 not readable/hardware-owned. Not a blocker.

## 3d: channel switch (v0.9.0, written 2026-10-07, awaiting hardware test)
- `setChannel()` ports `rtl8188eu_config_channel` (8188e.c:423), **20 MHz only**; order per core.c:6838-6840: set_tx_power, then config_channel.
  Writes BW_OPMODE 20MHz, clears FPGA0/FPGA1 RF_MODE bit0, RF18 channel (mask 0x3ff) then BW bits (10|11).
- Probe switches to ch6 then ch11 and logs `set_channel OK ...: RF18[11:0] ch6=0xc06 ch11=0xc0b` (expected values are my computation, not observed).
- Not done: 40 MHz, phy_iq_calibrate, RX path (bulk IN), TX.

## 4a: passive RX scan (v0.10.0, written 2026-10-07, awaiting hardware test)
Bundle: v0.8.1 (RFSW PAPE fix) + v0.9.0 (setChannel) + RX scan, none of 0.8.1/0.9.0 tested separately yet.
- `rxScan()`: channels 1-13 at 20 MHz, 350 ms dwell each (~4.5 s, blocks `start()`), synchronous bulk IN (4096-byte buffer, 100 ms io timeout).
- Parses rxdesc16 (24 bytes; bit positions derived from the LE bitfield layout in rtl8xxxu.h:135, NOT observed yet). Skips rpt_sel!=0 (C2H)
  and crc32 errors. Logs beacons/probe responses: SSID (tag 0), BSSID (addr3), DS channel (tag 3). Aggregated URBs (pkt_cnt>1) are not walked;
  aggregation is disabled by init_aggregation so this should not occur.
- Expected log: `rx_scan AP: ssid="..." ...` lines, per-channel `rx_scan chN: frames=.. mgmt=..`, and `rx_scan done`.
- Reading results: frames=0 on every channel and only timeouts -> RX path/RF not working (check `rx_scan start: RCR/CR`).
  frames>0 but mgmt=0 -> descriptor offsets suspect. crc_bad dominating -> RF/channel/IQ-cal problem.
