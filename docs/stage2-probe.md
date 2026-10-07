# Stage 2 probe — RTL8188EUProbe.kext (read-only)

**Scope:** test-protocol steps 1-2 only. Attaches to the dongle's `IOUSBHostInterface`
(0bda:8179, interface 0), logs endpoints, reads `REG_SYS_CFG` (0xF0) via vendor control request.
No register writes, no firmware, no Wi-Fi. Not an IO80211 driver.

## Build
```
./AirPort-RTW/scripts/bootstrap-deps.sh   # once; provides MacKernelSDK
make usbprobe                             # -> build/out/RTL8188EUProbe.kext
```
Compiles clean (`-Wall`). **Untested on hardware.**

## Protocol facts used (from Linux rtl8xxxu, verified 2026-10-07)
bmRequestType 0xC0 (read), bRequest 0x05, wValue = register address, wIndex = 0, timeout 500 ms
(`rtl8xxxu.h` / `core.c rtl8xxxu_read32`). `REG_SYS_CFG` = 0x00F0; cut = `(v & 0xF000) >> 12`;
cut 8 (I) and bit 23 (test chip) are rejected by rtl8xxxu (`8188e.c rtl8188eu_identify_chip`).

## Do NOT
- Do not install chris1111's "Wireless USB Big Sur Adapter" (can match the same device).
- Do not replace your only working EFI; keep a bootable fallback.

## Test (safest first: load by hand, no OpenCore injection)
1. Dongle stays plugged in. If `build/` is root-owned from an earlier manual chown: `sudo chown -R $USER build`
2. `make load` (copies to a root-owned staging dir, then `kextutil -v`)
   (needs SIP/AMFI relaxed as usual on your Hackintosh; if it refuses, send the output as-is.)
3. Check attach: `ioreg -l -w0 | grep -i RTL8188EUProbe`
4. Read the log: `log show --last 5m --predicate 'eventMessage CONTAINS "RTL8188EUProbe"'`
   (also try `sudo dmesg | grep RTL8188EUProbe`)
5. Unplug/replug the dongle; confirm no panic and the log shows a fresh attach.
6. `make unload`

## If macOS asks for approval + restart (Tahoe)
Use the persistent path instead of `make load` (its staging dir under /private/tmp is wiped on reboot):
`make install` -> approve in System Settings > Privacy & Security (if shown) -> restart -> run steps 3-5.
Undo: `make uninstall` + restart. Keep a bootable fallback EFI before restarting. The approval flow on
a Hackintosh with relaxed SIP is **not verified by me**; send the exact prompt text if it differs.

## Expected log
```
RTL8188EUProbe: attached: interface 0 class 0xff endpoints 3
RTL8188EUProbe: endpoint 0x.. dir=IN/OUT type=2   (x3)
RTL8188EUProbe: bulk IN ok, bulk OUT count 2       (count is a guess: not yet verified)
RTL8188EUProbe: REG_SYS_CFG(0x00f0) = 0x........ cut=N
```
Pass = attach + endpoints + a plausible `SYS_CFG` (not 0x00000000 / 0xFFFFFFFF), cut != 8.

## If it fails, send back
`kextutil -v` output, the log lines above, `kextstat | grep -i RTL8188`, and any panic report in
`/Library/Logs/DiagnosticReports/`.

## Known risks (honest list)
- Control transfer is issued inside `start()`; `IOUSBHostInterface::deviceRequest` takes the
  workloop lock — if it deadlocks, move the read to a thread/timer (first thing to try).
- Another driver may already own the interface (ioreg showed `IOUSBHostInterface@0` matched);
  our probe score is 5000 but Apple's generic driver behaviour on Tahoe is unverified.
- Match is on the interface; `open()` may fail if something holds it.
- Link-time: the kext uses `StandardUSB::getEndpoint*` / `getNextAssociatedDescriptorWithType`
  (undefined symbols resolved at load via IOUSBHostFamily). Linked with `-undefined dynamic_lookup`,
  so a missing export only shows up in `kextutil -v` ("symbol not found") — send that output if so.
