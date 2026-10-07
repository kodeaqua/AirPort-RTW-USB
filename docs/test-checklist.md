# Hardware test checklist for probe v0.13.0 (covers 4a drain fix, 4b TX, 4c link regs, 4d async engine)

Written 2026-10-07. One build, one load, one log. Nothing below has run on hardware yet.

## Build / load
```
cd ~/Projects/usbwifi-native-bringup
make                                   # -> build/out/RTL8188EUProbe.kext (v0.13.0)
sudo kmutil unload -b io.github.kodeaqua.RTL8188EUProbe     # remove the old one if loaded
make load                              # or: make install (+ approve in Privacy & Security, reboot)
```
Make sure the chris1111 USB package and the old rtw88.kext are not installed/loaded.

## Collect (one command)
```
/usr/bin/log show --last 10m --predicate 'sender == "RTL8188EUProbe"' --info | grep -E 'rx_scan|set_mac|link_selftest|async_selftest|rx_stop|init_wmac|set_channel'
kmutil showloaded | grep -i 8188
```
If the machine panics: send the newest file from `/Library/Logs/DiagnosticReports/` (and the last lines of the log if it survived).

## What each line proves, and the pass criteria
| Log line | Pass | If not |
|---|---|---|
| `rx_scan: set_mac OK ... active probe requests on` | set_mac OK | MACID write failed: USB control path |
| `rx_scan chN: ... probe_resp=.. tx_ok=1 tx_fail=0` | tx_ok=1 on all 13 channels; probe_resp>0 on ch9-13 for AP "Rumah 4G" | tx_ok=1 but probe_resp=0 everywhere: chip not transmitting (txdesc/queue/tx power/MAC). tx_fail>0: bulk OUT pipe error, send the (0x........) code |
| `rx_scan AP: ... heard_on=` | heard_on in 9..13 (drain fix) | still 1: stale-frame theory wrong |
| `link_selftest: ... MSR=0x02 BSSID=02:11:22:33:44:55 INIRTS=4 SLOT=9 BCN_PSR_RPT lo16=0xc001` | all as printed in "expect" | a wrong value shows which register ignores writes |
| `link_selftest: set_key=0x0 (cam 0) set_key2=0x0 (cam 1) ... bit31 should be 0` | CAM_CMD after clear has bit31 = 0; SECCFG=0xcf | CAM polling bit stuck: CAM write path broken |
| `link_selftest: join(+H2C media status)=0x0 leave(+H2C)=0x0 next_mbox=2 HMTFR=0x00` | both 0x0, next_mbox=2 (two H2C sent) | 0x...bs (busy): firmware not consuming mailbox, i.e. firmware not running correctly |
| `async_selftest: ... beacons=~19 ... rx completed ... tx submitted=5 completed=5 errors=0 ... tx_done_cb=5` | beacons 12..22 (one AP, 102 ms interval, 2 s), tx completed=5, errors=0 | beacons far below 19: RX loses frames (sensitivity / IQ cal / crystal cap not done); tx completed<5: async OUT broken |
| `rx_stop: N RX completion(s) still outstanding` | must NOT appear | abort/drain broken, unsafe for unplug |

## Quick interpretation
- Everything passes: TX descriptor, queues, CAM, H2C and async I/O are proven; WP4-6 (frontend shim + IO80211 provider) can start on solid ground.
- TX does not reach the air: fix txdesc first. Do not start the frontend integration on top of it.
- Beacon count low but nonzero: acceptable for now; schedule phy_iq_calibrate + set_crystal_cap.
