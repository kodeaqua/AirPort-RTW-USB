# Stage 0 recon — where a second core + USB transport can plug in

Source read: AirPort-RTW clone (`src/kext`, `src/compat`, `Makefile`, `scripts/bootstrap-deps.sh`).
No code was changed. Nothing was built or run on hardware.

## 1. Layering (as found)

```
IO80211Controller  AirPortRTW (src/kext/AirPortRTW.cpp, 3501 lines)
   │  also implements RTW88RxDelegate + RTW88HwOps + RTW88EventDelegate
   ▼
RTW88IEEE80211 (src/kext/RTW88IEEE80211.cpp, 4811 lines)   ← scan/auth/assoc/4-way/TX/RX state machine
   │  drives the core through struct ieee80211_hw / hw->ops   (compat mac80211.h shim)
   ▼
Linux rtw88 core  (fetched by bootstrap-deps.sh from github.com/thegwchr/rtw88-stable)
   │  PCI calls go through shim: pci_ops_rtw88 + rtw88_dma_alloc_ops (src/kext/RTW88HwOps.cpp)
   ▼
IOPCIDevice (provider)
```

## 2. Where PCIe attaches to the core

- `RTW88HwOps` (RTW88IEEE80211.hpp:72) is a virtual interface: PCI config R/W, `mmioBase()`,
  DMA alloc/free/bounce. `AirPortRTW` implements it; `RTW88HwOps.cpp` exposes it to the C core
  as `_pci_io_ops` / `_dma_ops` via global `g_pci_dev_instance`.
- The C core reaches MMIO through `ioremap` returning `mmioBase()`. **USB has no MMIO**: rtw88/rtl8xxxu
  access registers by vendor control transfers, so this interface does not fit USB.

## 3. Where the frontend calls into the core

Mostly through the mac80211 ops table (`_hw->ops->start/stop/add_interface/sta_add/set_key/
hw_scan/cancel_hw_scan/bss_info_changed/configure_filter/tx`) — a narrow, standard interface.
`_rtwdev->` is never dereferenced in RTW88IEEE80211.cpp (0 hits).

**But the seam is not clean.** Hard rtw88 coupling:

| Where | What |
|---|---|
| RTW88IEEE80211.cpp:35,842 | direct call `rtw_pci_probe()` / `rtw_pci_remove()` |
| RTW88IEEE80211.cpp:42-44, 821-830 | `rtw88_pci_chip_table` → `rtw8822b/8822c/8821c_hw_spec` (`struct rtw_chip_info`) |
| RTW88IEEE80211.cpp:859 | `_rtwdev = _hw->priv` (assumes rtw_dev layout) |
| compat `rtw88_*` helpers (rtw88_compat.c) | ~25 helpers: `rtw88_connect_hw_setup` (calls `rtw_set_channel`, `rtw_vif_port_config`), `rtw88_hw_scan_supported`, `rtw88_sw_scan_*`, `rtw88_get_stats`, `rtw88_get_fw_version`, `rtw88_get_chip_name`, `rtw88_parse_peer_caps`, `rtw88_get_tx_nss`, AWDL channel helpers |
| compat `rtw88_get_hw()` / `rtw88_register_hw()` | single global `g_rtw88_hw` — **one device at a time**, no PCIe+USB coexistence without refactor |

Conclusion: the *mac80211 ops* part is reusable for a mac80211-style core (rtl8xxxu); the
`rtw88_*` compat helpers are rtw88-specific and would need an 8188EU equivalent (or a chip-neutral
abstraction) for connect setup, scan, stats.

## 4. Existing USB scaffolding — stale, not built

- `src/kext/RTW88USBDevice.{hpp,cpp}` (IOUSBHost, bulk/ctrl via `io()` / `deviceRequest()`) exists but
  is **excluded**: Makefile comment "PCIe-only project: USB/SDIO device wrappers and transport
  backends are intentionally excluded"; neither `airport` nor `kext` target lists it.
- It is an `IOEthernetController` (rtw88.kext style), not IO80211.
- It references `rtw_usb_probe`, `rtw88_usb_io_ops`, `struct rtw88_usb_ops` and a "usb.c" that is
  not in the Makefile (`DRIVER_SRCS` has only `pci.c`). Not verified whether those symbols resolve.
- Design flaws visible by reading: synchronous `io()` only (no async bulk/completions), only first
  bulk IN/OUT pipe (8188EU has multiple OUT endpoints for TX queues — *to verify on hardware*),
  global `g_usb_dev`, `freeCoherent` leaks, `reinterpret_cast<RTW88PCIDevice*>(this)` type punning.
  Treat as a reference sketch only, not a base to trust.
- `AirPortRTW.kext/Contents/Info.plist` matches only `IOPCIDevice` + `IOPCIMatch` (lines 68-104).
  A USB personality (`IOUSBHostDevice`, idVendor/idProduct) is needed.

## 5. Build facts (from Makefile)

- `make airport` → `build/out/AirPortRTW.kext`; objects = rtw88 core + chips (8822b/8822c/8821c) +
  compat + firmware blobs + `AIRPORT_KEXT_SRCS`. Flags `-DCONFIG_RTW88_PCI=1`, MINOS 13.0, x86_64.
- Firmware is embedded (`gen_fw_blobs.py` → `fw_blobs.c`); bootstrap downloads from X1REN41L's repo.
- Baseline `make airport` has **not** been run in this session (needs bootstrap downloads).

## 6. Seam recommendation

Plug in at the **mac80211 ops level**, as a second `ieee80211_hw`:
`USB transport (IOUSBHost)  →  rtl8xxxu-derived core (ieee80211_ops)  →  existing RTW88IEEE80211`.
Required refactors before a second core can attach:
1. Replace `rtw_pci_probe` + `rtw88_pci_chip_table` in `RTW88IEEE80211::start()` with a core-selection hook.
2. Abstract the ~25 `rtw88_*` compat helpers behind a per-core vtable (or give the 8188EU core its own).
3. Replace the single-global `g_rtw88_hw` with per-instance state (at minimum, guard against dual load).
4. New AirPortRTW-USB provider class (IO80211Controller on `IOUSBHostDevice`), not `IOEthernetController`.
   `AirPortRTW` is currently `IOPCIDevice`-specific (AirPortRTW.hpp:21 includes IOPCIDevice).
   Not yet read: how much of AirPortRTW.cpp (3501 lines) touches `_pciDevice` directly — **read before Stage 2**.

## 7. Still unknown (needs user)

- Dongle VID:PID (`system_profiler SPUSBDataType`)
- macOS version of the Hackintosh
- 8188EU silicon cut (rtl8xxxu does not support cut I — see decisions.md)
