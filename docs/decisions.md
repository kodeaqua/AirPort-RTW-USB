# Decisions

## D1 — 8188EU core: Linux `rtl8xxxu` (proposed, pending Stage 0 sign-off)

**Evidence (verified 2026-10-07):**
- `drivers/net/wireless/realtek/rtl8xxxu/Kconfig` in torvalds/linux lists RTL8188EU among supported chips
  (Kconfig help text). Files: `8188e.c` (~55 KB), `core.c` (~238 KB), `regs.h`, `rtl8xxxu.h`.
- `8188e.c` is `GPL-2.0-only` (compatible with this repo's GPL-2.0; note rtw88 is GPL-2.0 OR BSD-3).
- Firmware: `rtlwifi/rtl8188eufw.bin` (name matches CLAUDE.md expectation). Redistribution terms
  **not yet checked** → fetch from linux-firmware in `scripts/`, do not commit.
- Staging `r8188eu` was deleted from Linux in March 2023 because rtl8xxxu covers the same devices.
- Limitation stated in `8188e.c`: **"RTL8188EU cut I is not supported"**. Check dongle cut early.
- rtl8xxxu limitations (Kconfig): no 40 MHz, no power management. Acceptable (goal: 2.4 GHz 1T1R 11n).

**Why over vendor `rtl8188eus`:** it is a mac80211 driver, so it speaks the same `ieee80211_ops` that
`RTW88IEEE80211` already drives (recon.md §3). Vendor HAL would need its own cfg80211-less frontend.

**Costs:** `core.c` is large and uses more Linux APIs than rtw88 (USB URBs, workqueues, firmware,
rate-control hooks) → the compat headers (`linux/usb.h`, etc.) will need real USB semantics.
rtl8xxxu does software rate/connect handling in places (see `report_connect`, `update_rate_mask`),
which interacts with `rtw88_connect_hw_setup`-style shortcuts — to be designed in Stage 4.

**Rejected:** extending rtw88 (lwfinger/rtw88 has no 8188EU).

## Open before Stage 1 is final
- Read AirPortRTW.cpp PCI dependence; confirm USB provider class design.
- Get VID:PID and macOS version from user.
