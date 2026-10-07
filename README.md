# usbwifi-native-bringup

Native macOS Wi-Fi (menu bar, scan, join WPA2) for a **Realtek RTL8188EU USB dongle** (for example the TP-Link
`0bda:8179` "802.11n NIC") on a Hackintosh, by extending
[AirPort-RTW](https://github.com/JoMei9019-real/AirPort-RTW) with a USB transport and a second hardware core.

This is **not** a standalone utility app and it is **not** the prebuilt "Wireless USB Big Sur Adapter" package.
The dongle shows up as a normal IO80211 interface (`en1`) managed by macOS itself.

> **Status: experimental kernel code.** A bug can panic the machine. Keep a bootable fallback EFI, and read
> [docs/INSTALL.md](docs/INSTALL.md) before loading anything.

## What works

Verified on hardware (probe kext and the integrated driver up to join + ping, 2026-10-07/08, macOS 26.7.1 Tahoe):

| Area | State |
|---|---|
| USB attach, chip identify, efuse (MAC), power-on, firmware download, MAC/BB/RF init | works |
| Passive and active scan, TX over the air, CCMP hardware keys | works |
| Join WPA2 (CCMP), DHCP, ping, internet | works (5/5 ping, 0% loss; occasional duplicate replies, under investigation) |
| Signal bars (per-frame RSSI from PHY status) | works |
| Sleep/wake, hot-unplug, software rate adaptation, RSSI start rate | **implemented, not yet tested on hardware** (v0.18.0 to v0.20.0) |

Not supported yet: 11n/HT (no MCS rates, 20/40 MHz HT, A-MPDU), TKIP/WEP, 5 GHz (the chip is 2.4 GHz 1T1R),
AP mode, BT coexistence. Maximum link is therefore legacy 54 Mbps (realistically about 20-25 Mbps).

Per-release detail: [CHANGELOG.md](CHANGELOG.md). Test steps and what to send back on failure:
[docs/stage5-test.md](docs/stage5-test.md).

## How it fits together

```
IO80211 / CoreWiFi
      |
AirPortRTW (frontend from AirPort-RTW, unchanged except patches/)
      |  mac80211-shaped hw->ops (src/usb/RTL8188EUHw.c)
RTL8188EUBridge.cpp  --  plain-C bridge (separate TU because of header clashes)
      |
RTL8188EUCore.cpp   -- hand port of Linux rtl8xxxu (RTL8188EU): efuse, power, firmware, init, TX/RX, rate adaptation
      |  IOUSBHost (control + bulk IN/OUT)
RTL8188EU dongle
```

One `AirPortRTW.kext` matches both the original PCIe devices and the USB dongle. A second, standalone
`RTL8188EUProbe.kext` (bring-up/debug tool, no Wi-Fi interface) exists but **must not be loaded together** with
`AirPortRTW`.

Repository layout:

| Path | Content |
|---|---|
| `src/usb/` | RTL8188EU core, USB bridge, mac80211 face, pure helpers (`rtl8188eu_txdesc.h`, `rtl8188eu_ra.h`) |
| `patches/` | Patches applied to the (gitignored) `AirPort-RTW/` clone: `0001-core-ops`, `0002-usb-provider`, `0003-usb-pm-hotplug` |
| `kext/` | Info.plist of the standalone probe kext |
| `scripts/` | Firmware fetch, table generator, patch applier, dmesg capture |
| `tools/` | Host-side unit tests (`make test-txdesc`, `make test-ra`) |
| `docs/` | Recon, decisions, stage notes and hardware test plans |

## Quick start

Full details: [docs/BUILD.md](docs/BUILD.md) and [docs/INSTALL.md](docs/INSTALL.md).

```sh
git clone https://github.com/kodeaqua/usbwifi-native-bringup.git
cd usbwifi-native-bringup
git clone https://github.com/JoMei9019-real/AirPort-RTW.git AirPort-RTW
(cd AirPort-RTW && ./scripts/bootstrap-deps.sh)
make airport-usb          # -> AirPort-RTW/build/out/AirPortRTW.kext
```

Then inject the kext with OpenCore (`Kernel > Add`, `MinKernel 22.0.0`), reboot, and plug in the dongle.
On Sonoma/Sequoia/Tahoe the restored legacy Wi-Fi stack (IOSkywalkFamily + IO80211FamilyLegacy) is required.

## Requirements

- x86_64 Hackintosh with OpenCore; macOS Ventura IO80211 ABI. Sonoma/Sequoia/Tahoe need the restored legacy stack
  (see `AirPort-RTW/OPENCORE_GUIDE.txt`). Monterey and earlier are not validated.
- Xcode Command Line Tools, `python3`, `curl`, `rsync`, `git` for building.
- RTL8188EU chip cut A-H (cut I is rejected; the author's dongle is cut D). Dongle ID `0bda:8179` is the only one
  in the match personality so far.

## Safety rules

- Never load the old Feixiao `rtw88.kext` together with `AirPortRTW` (same PCI devices).
- Never install the chris1111 "Wireless USB Big Sur Adapter" package while testing (it can match the same device).
- Do not load `RTL8188EUProbe.kext` together with `AirPortRTW.kext`.
- Keep a bootable fallback EFI. Never replace your only working EFI.

## License

GPL-2.0-only, see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md). Large parts are ported from the Linux `rtl8xxxu`
driver; the firmware blob (`rtl8188eufw.bin`, Realtek license) is downloaded at build time and must not be
redistributed or committed.
