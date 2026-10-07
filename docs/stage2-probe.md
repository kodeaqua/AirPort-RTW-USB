# Stage 2 probe — RTL8188EUProbe.kext

## Result (confirmed on hardware, 2026-10-07, v0.1.1)
Test-protocol step 1 PASS, step 2 (chip ID) PASS:
```
attached: interface 0 class 0xff endpoints 3
endpoint 0x81 IN bulk, 0x02 OUT bulk, 0x03 OUT bulk   -> bulk IN ok, bulk OUT count 2
REG_SYS_CFG(0x00f0) = 0x24403735 cut=3
```
cut 3 = 'D' (`'A' + chip_cut`, rtl8xxxu core.c); only cut 8 (I) and bit 23 (test chip) are rejected.
Benign: kernelmanagerd logs "Signing information did not contain a cdhash" (ad-hoc signed) yet the kext
is approved and loads. `dmesg` shows nothing on modern macOS; use `/usr/bin/log show` (plain `log` is
shadowed by a zsh builtin).

## v0.2.0 scope (step 2b: MAC from efuse) — untested on hardware
Adds `regWrite` and a port of `rtl8xxxu_read_efuse` / `rtl8xxxu_read_efuse8`. This is **no longer
strictly read-only**: like Linux (before power-on) it writes `REG_EFUSE_ACCESS`(0xCF)=0x69 (restored
to 0x00 afterwards), and sets `SYS_ISO_CTRL`.PWC_EV12V, `SYS_FUNC`.ELDR, `SYS_CLKR` loader+ANA8M only if
not already set. No firmware, no Wi-Fi. Not an IO80211 driver. Efuse layout (`struct rtl8188eu_efuse`):
rtl_id (LE16) @0x00 must be 0x8129, MAC @0xD7. The diagnostic device-level personality was removed.

## Build
```
./AirPort-RTW/scripts/bootstrap-deps.sh   # once; provides MacKernelSDK
make usbprobe                             # -> build/out/RTL8188EUProbe.kext
```
Compiles clean (`-Wall`). v0.1.1 confirmed on hardware; v0.2.0 efuse read not yet.

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

v0.2.0 adds, after the SYS_CFG line:
```
RTL8188EUProbe: 9346CR=0x.... boot=EFUSE|EEPROM
RTL8188EUProbe: efuse rtl_id=0x8129 (OK, expect 0x8129)
RTL8188EUProbe: efuse MAC xx:xx:xx:xx:xx:xx
RTL8188EUProbe: efuse[000] ... (16 rows)
```
Pass = rtl_id OK and a MAC that is not ff:ff:.. / 00:00:.. (multicast bit of byte 0 should be 0).
If rtl_id MISMATCH or all 0xff: send the 16 `efuse[...]` rows, do not proceed to firmware.

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

## Result: v0.2.0 efuse (2026-10-07) — PASS
```
9346CR=0x0020 boot=EFUSE (EEPROM present)
efuse rtl_id=0x8129 (OK, expect 0x8129)
efuse MAC 50:3d:d1:6d:14:6a
```
Efuse bytes 0xD0..0xD3 = `da 0b 79 81` (VID 0bda, PID 8179), matches `ioreg -p IOUSB`.
Efuse read ~430 ms (00.5299 -> 01.9570), acceptable for a one-time probe.

## v0.3.0 — Stage 3b-1: MAC power_on only (no firmware)
Ported from Linux `rtl8xxxu` `8188e.c` (`rtl8188e_disabled_to_emu`, `rtl8188e_emu_to_active`,
`rtl8188eu_power_on`), fetched from torvalds/linux master 2026-10-07. Runs after the efuse dump.
Writes: APS_FSMCO (0x04), SYS_FUNC (0x02), AFE_XTAL_CTRL (0x24), LPLDO_CTRL (0x23), CR (0x100).
Does not set MAC TX/RX enable (source notes a 8188E hw bug), no firmware, no queue init.

Expected new log line:
```
power_on OK (0x00000000): CR=0x06ff? SYS_CLKR=0x.... (MAC_CLK on) APS_FSMCO=0x........
```
(CR value is not predicted; the point is OK + MAC_CLK on.) Pass = `power_on OK` and `MAC_CLK on`.
If FAILED: send the `powerOn:` lines (they name the step and FSMCO value). Do not proceed to firmware.
Next (3b-2): `rtl8xxxu_download_firmware` + `start_firmware` (core.c), needs `rtl8188eufw.bin`.

## v0.4.1 — Stage 3b-2: firmware download + start: PASS (2026-10-07)
Hardware log:
```
firmware 15262 bytes, revision 28.0 signature 0x88e1
initQueues: 2 bulk OUT, no low queue
firmware RUNNING (0x00000000): FW_DL=0x000300c6
```
Pass = `firmware RUNNING`. (v0.4.0 failed in initQueues on the 2-OUT-endpoint dongle; fixed in v0.4.1.)
Next (3c): LLT init, MAC/BB/RF init per `rtl8xxxu_init_device`; verify against source first.

## v0.5.0 — Stage 3c-1: init_mac (MAC register table)
Port of `rtl8xxxu_init_mac` (core.c:2187), RTL8188E branch: 92 8-bit writes from `rtl8188e_mac_init_table`
(generated into `build/fw/rtl8188eu_tables.h` by `scripts/gen-rtl8188eu-tables.py`, `make usbprobe` runs it),
then `REG_MAX_AGGR_NUM` (0x4ca) 16-bit = 0x0707. Runs right after `firmware RUNNING`, same as Linux order.

Expected new log line:
```
init_mac OK (0x00000000): MAX_AGGR_NUM=0x0707 (expect 0x0707) 0x428=0x0a (expect 0x0a) 0x652=0x20 (expect 0x20)
```
Pass = `init_mac OK` and `firmware RUNNING` still present. Readbacks are informational (a register may
legitimately not read back what was written; report it, do not assume failure).
If FAILED: send the `initMac:` line (names the register). Do not proceed to BB init.
Next (3c-2): RF register accessor + `init_phy_bb`.
