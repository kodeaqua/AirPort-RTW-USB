# Installing and testing

> Kernel code. Keep a bootable fallback EFI and do not replace your only working one.

## Before you start

1. Build `AirPortRTW.kext` ([BUILD.md](BUILD.md)).
2. Remove anything that matches the same device:
   - the old Feixiao `rtw88.kext`,
   - the chris1111 "Wireless USB Big Sur Adapter" package,
   - `RTL8188EUProbe.kext` (`make uninstall`).
3. Sonoma, Sequoia and Tahoe: install the restored legacy Wi-Fi stack (IOSkywalkFamily and IO80211FamilyLegacy from the
   OpenCore Legacy Patcher payloads). The kext cannot load without it. Follow `AirPort-RTW/OPENCORE_GUIDE.txt`; its
   "OpenCore setup by macOS version" section is the reference and is not duplicated here.

## OpenCore injection

1. Copy `AirPort-RTW/build/out/AirPortRTW.kext` to `EFI/OC/Kexts/`.
2. Add it to `config.plist` under `Kernel > Add`:

   | Key | Value |
   |---|---|
   | BundlePath | `AirPortRTW.kext` |
   | ExecutablePath | `Contents/MacOS/AirPortRTW` |
   | PlistPath | `Contents/Info.plist` |
   | MinKernel | `22.0.0` |
   | Enabled | `true` |

3. Reboot and plug the dongle in. Its USB personality is `AirPortRTW_8188EU_USB` (interface 0 of `0bda:8179`).

## Check that it works

```sh
kmutil showloaded | grep -i AirPortRTW
ioreg -p IOService -w0 | grep -i AirPortRTW
/usr/bin/log show --last 5m --predicate 'sender == "AirPortRTW"' --info
sudo dmesg | grep -E "AirPortRTW|RTL8188EU|rtw88:"
```

Note: `IOLog` from the kext does not reach `log show` on every setup; `sudo dmesg` right after boot is the reliable source
(its buffer is small, so avoid waiting long). `sudo scripts/capture-dmesg.sh` keeps every unique driver line in a file.

Expected log lines: `USB RTL8188EU core attached`, `initHardware OK, MAC xx:xx:...`, `IEEE80211::start complete - SUCCESS`.
Then a Wi-Fi interface appears in System Settings and the menu bar; scan, join a WPA2 (CCMP) network, `ping` the gateway.

## Hardware test order (one step per reboot)

1. kext loads and attaches to the dongle.
2. register reads return a sane chip ID; MAC comes from efuse.
3. firmware download succeeds.
4. scan lists nearby 2.4 GHz networks.
5. join WPA2, ping works.
6. sleep/wake; unplug/replug (works on hardware as of 1.0.0).
7. auto-join after wake/replug: **known not to work in 1.0.0**; join manually from the menu bar. Patch 0004 attempts a fix and
   is untested. To check, wake from sleep, wait about 30 s and see whether the known network joins by itself. If not, send
   `/usr/bin/log show --last 3m --info --debug --predicate 'process == "airportd"' | grep -E "AUTO-JOIN|Unexpected event payload"`
   together with `sudo dmesg`.

## If something fails

Do not iterate blindly; send the logs first:

```sh
sudo dmesg | grep -E "AirPortRTW|RTL8188EU|rtw88:"
kmutil showloaded | grep -iE "rtw|8188"
ls -t /Library/Logs/DiagnosticReports | head      # panic reports
```

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for known symptoms.
