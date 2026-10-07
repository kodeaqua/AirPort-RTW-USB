# Troubleshooting

| Symptom | Likely cause / what to check |
|---|---|
| Kext does not load on Sonoma/Sequoia/Tahoe | The restored legacy Wi-Fi stack (IOSkywalkFamily, IO80211FamilyLegacy) is missing. See `AirPort-RTW/OPENCORE_GUIDE.txt`. |
| Nothing attaches to the dongle | Another driver owns the device (chris1111 package, `RTL8188EUProbe.kext`, old `rtw88.kext`). Remove it. Confirm the ID with `ioreg -p IOUSB`; only `0bda:8179` is matched. |
| `initHardware ... FAILED` | Send the whole `RTL8188EU` log. Chip cut I is rejected by design; the log shows the cut (`SYS_CFG`). |
| Scans work but join never finishes | Capture `rx UC` lines right after the join attempt (`sudo dmesg \| grep -E "rx UC\|rtw88: DIAG"`). EAPOL frames show as `ethertype=0x888e`; encrypted data as `dec=1`. |
| Ping shows `DUP!` replies | Known, under investigation. Send `sudo dmesg \| grep -E "rx DUPSEQ\|tx ICMP"` taken right after a 30-packet ping. |
| Slow throughput | No HT/11n yet; legacy rates only. The rate comes from software rate adaptation (v0.20.0); look for `ra:` lines. |
| Radio dead after sleep | Send the `resume check` / `wake complete` / `PM transition ... failed` lines. Unplug and replug the dongle as a workaround. |
| Panic | Send the newest file in `/Library/Logs/DiagnosticReports/` and the log lines before it. |

Log prefix `RTL8188EUProbe:` also appears with the integrated kext: it is the core's log prefix, not the probe kext.
