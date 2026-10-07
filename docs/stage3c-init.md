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
