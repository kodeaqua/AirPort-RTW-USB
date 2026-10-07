# Troubleshooting

| Symptom | Likely cause / what to check |
|---|---|
| Kext does not load on Sonoma/Sequoia/Tahoe | The restored legacy Wi-Fi stack (IOSkywalkFamily, IO80211FamilyLegacy) is missing. See `AirPort-RTW/OPENCORE_GUIDE.txt`. |
| Nothing attaches to the dongle | Another driver owns the device (chris1111 package, `RTL8188EUProbe.kext`, old `rtw88.kext`). Remove it. Confirm the ID with `ioreg -p IOUSB`; only `0bda:8179` is matched. |
| `initHardware ... FAILED` | Send the whole `RTL8188EU` log. Chip cut I is rejected by design; the log shows the cut (`SYS_CFG`). |
| Scans work but join never finishes | Capture `rx UC` lines right after the join attempt (`sudo dmesg \| grep -E "rx UC\|rtw88: DIAG"`). EAPOL frames show as `ethertype=0x888e`; encrypted data as `dec=1`. |
| Ping shows `DUP!` replies | Known, under investigation. Send `sudo dmesg \| grep -E "rx DUPSEQ\|tx ICMP"` taken right after a 30-packet ping. |
| Slow throughput | Rate comes from software rate adaptation (v0.20.0) over HT20 MCS0-7 (v0.21.0), no TX aggregation. Look for `ra:` lines (`init mask=... start rate`, `rate idx A -> B`) and `peer:` (AP rate set, `ht=1`). |
| Join fails or drops only on 11n APs | Try the same AP with HT disabled to confirm (e.g. 11g-only/legacy mode) and send the `peer:` and `ra:` lines; HT is new in v0.21.0. |
| Radio dead after sleep | Send the `resume check` / `wake complete` / `PM transition ... failed` lines. Unplug and replug the dongle as a workaround. |
| Panic | Send the newest file in `/Library/Logs/DiagnosticReports/` and the log lines before it. |

Log prefix `RTL8188EUProbe:` also appears with the integrated kext: it is the core's log prefix, not the probe kext.
