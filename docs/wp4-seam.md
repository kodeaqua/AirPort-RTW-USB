# WP4 seam: what the frontend needs from a core (measured 2026-10-07)

Source: `AirPort-RTW/src/kext/RTW88IEEE80211.cpp` (4811 lines) and `AirPortRTW.cpp` (3501 lines).

## Hard PCI/rtw88 coupling in RTW88IEEE80211.cpp
- `start()` (~L822-863): chip lookup from `_pcidev->device` via `rtw88_pci_chip_table`, then `rtw_pci_probe()`.
  After it, the frontend only uses `rtw88_get_hw()` -> `ieee80211_hw` (`hw->ops`, `hw->wiphy->perm_addr`, `hw->priv`).
- `stop()` / `fail_probe` (~L933, L982): `rtw_pci_remove(_pcidev)`.
- `create(RTW88PCIDevice*, pci_dev*)` (~L525): factory takes PCI types.
- Hook callbacks `rtw88_hw_callbacks {rx_frame, tx_status, scan_done}` are the RX/TX-status/scan-done path from core to frontend.

## Frontend -> core contract (mac80211 `hw->ops`, 13 ops)
tx, start, stop, config, add/remove_interface, configure_filter, bss_info_changed, hw_scan, cancel_hw_scan,
sta_add/remove, set_key.

## Helper symbols used by the frontend (count of call sites, both files)
Data path: `rtw88_make_packet_mbuf`(4), `rtw88_be_tx_avail`(6), `rtw88_be_tx_busy`, `rtw88_set_tx_resume_cb`(2),
`rtw88_trigger_interrupt`(4), `rtw88_set_hw_callbacks`(4), `rtw88_get_tx_nss`.
Link/state: `rtw88_register_hw/vif/sta`, `rtw88_unregister_vif/sta`, `rtw88_connect_hw_setup`(3), `rtw88_restore_*`(3),
`rtw88_set_station_mac`, `rtw88_is_scanning`, `rtw88_hw_scan_supported`, `rtw88_sw_scan_start/complete/switch_channel`.
Caps: `rtw88_peer_caps`(4), `rtw88_parse_peer_caps`(3), `rtw88_restrict_peer_caps`(2).
Info: `rtw88_get_stats`, `rtw88_get_fw_version`, `rtw88_get_chip_name`, `rtw88_find_fw_dir`, `rtw88_force_wifi_only`.
AWDL: `rtw88_awdl_*switch_channel` (can be stubbed for USB).
Misc: `rtw88_compat_init/exit`, `rtw88_copy_log`, `rtw88_debug_dump_tx_state_to`.
`rtw_pci_stop` (1 site) also needs a USB equivalent.

## Proposed shape (WP4)
1. New `RTW88CoreOps` vtable (patch file `patches/0001-core-ops.patch`, applied to the gitignored clone):
   `probe(), remove(), get_hw(), start/stop transport`. PCI implementation wraps the current calls unchanged
   (PCIe path must behave identically).
2. USB implementation (ours, in `src/usb/`): allocates an `ieee80211_hw` + `ieee80211_ops` whose ops call into
   the hardware-verified core (init sequence, `txMgmt`, async RX engine, CAM, H2C). RX delivers skbs via `rtw88_hw_callbacks`.
3. Replace `rtw88_pci_chip_table` lookup in `start()` by `ops->probe()`.
4. Stub (not implement) AWDL, `rtw88_trigger_interrupt`, and PCI-only debug helpers for USB.

## Answers from `compat/rtw88_compat.c` (1626 lines, read 2026-10-07)
- Link/scan helpers deref `rtw_dev` directly and call rtw88 core internals (`rtw_set_channel`, `rtw_vif_port_config`,
  `rtw_chip_prepare_tx`, `rtw_core_scan_start/complete`, `rtwdev->mutex/hal.rcr/dm_info/need_rfk`): `rtw88_sw_scan_start/complete/switch_channel`,
  `rtw88_connect_hw_setup`, `rtw88_restore_interface/connected_hw(_timeslice)`, `rtw88_set_station_mac`, `rtw88_hw_scan_supported`,
  `rtw88_is_scanning`, `rtw88_force_wifi_only`, `rtw88_awdl_*`.
- Info helpers deref `rtwdev->fw/chip/stats/efuse/hal`: `rtw88_get_fw_version/chip_name/stats/tx_nss`.
- PCI-only: `rtw88_be_tx_avail/busy`, `rtw88_trigger_interrupt`, `rtw88_reenable_interrupt`, `rtw88_debug_dump_tx_state*`
  (use `rtw_pci` ring/HISR registers; `g_irq_dev_id` cast to `rtw_dev`).
- Conclusion: faking a `rtw_dev` is NOT viable (too many fields, and those helpers would run rtw88 PHY code on 8188EU registers).
  The helpers must be dispatched through the core-ops vtable: PCI ops = current bodies unchanged, USB ops = our core.
  So `RTW88CoreOps` needs, beyond probe/remove: scan_start/complete, switch_channel, connect_setup, restore_*, set_station_mac,
  info getters, tx_avail/busy, is_scanning. Roughly 20 entries; frontend call sites are mechanical renames.

## Still open
- Resolved: the frontend only casts `hw->priv` to `rtw_dev*` once (RTW88IEEE80211.cpp:859) and passes it opaquely to the `rtw88_get_*` helpers; no field access. So `_rtwdev` can be an opaque USB core pointer once those helpers go through the vtable.
- Data-frame txdesc (QoS, rate, key) is still deferred in the core; needed for data after association.
