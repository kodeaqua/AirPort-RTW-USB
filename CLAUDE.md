# CLAUDE.md — AirPort-RTW USB support (RTL8188EU)

## Goal

Make a Realtek **USB** Wi-Fi dongle (TP-Link, **RTL8188EU**) work as a **native macOS Wi-Fi interface**
(menu bar, scan, join, WPA2) on a Hackintosh, by extending **AirPort-RTW**
(`github.com/JoMei9019-real/AirPort-RTW`).

Not the goal: a separate utility app, or the chris1111 "Wireless USB Big Sur Adapter" approach.
That package is a prebuilt binary with its own frontend, not IO80211. It is only a behavioral
reference, and it must NOT be installed while testing (it can match the same device).

## What is known (verified from READMEs)

- AirPort-RTW: native IO80211 driver, derived from the Feixiao rtw88 macOS port. Frontend is
  IO80211FamilyLegacy + IOSkywalkFamily. Hardware core is Linux-style rtw88.
- Currently **PCIe only**: RTL8821CE (10ec:c821, b821), RTL8822BE (b822), RTL8822CE (c822, c82f).
  RTL8812AE / 8814AE exist in the lineage but are disabled.
- Targets x86_64 Hackintosh, Ventura IO80211 ABI. Sonoma/Sequoia/Tahoe need the restored legacy
  Wi-Fi stack (IOSkywalkFamily + IO80211FamilyLegacy from OpenCore Legacy Patcher payloads).
  Monterey and earlier are not validated.
- Repo layout: `src/`, `ctl/`, `firmware/`, `scripts/` (`bootstrap-deps.sh`), `AirPortRTW.kext/Contents`,
  `Makefile`, `OPENCORE_GUIDE.txt`, `NOTICE.md`. Build: `./scripts/bootstrap-deps.sh && make airport`
  → `build/out/AirPortRTW.kext`.
- GPL-2.0. Keep SPDX/copyright headers. Update `NOTICE.md` for any code taken from other projects.
- `lwfinger/rtw88` (Linux backport) does **not** list RTL8188EU. Its USB chips are 8723DU, 8811AU,
  8811CU, 8812AU, 8812BU, 8812CU, 8814AU, 8821AU, 8821CU, 8822BU, 8822CU.
- Therefore the rtw88 core cannot drive the 8188EU. A different core is required.

## Progress (updated 2026-10-07)

Details: `docs/recon.md`, `docs/decisions.md`, `docs/stage2-probe.md`.

- **Stage 0 recon: done.** Frontend drives the core via mac80211 `hw->ops`, but is coupled to rtw88
  (`rtw_pci_probe`, chip table, ~25 `rtw88_*` compat helpers, single global `g_rtw88_hw`).
  PCI dependence in `AirPortRTW.cpp` is localized (start/interrupt/config/power); ioctl layer is shareable.
- **Baseline `make airport` builds OK** on macOS 26.7.1 (inside the `AirPort-RTW/` clone, which is
  gitignored; our own code lives in `src/usb/`, `kext/`, root `Makefile`).
- **Stage 1 core: `rtl8xxxu` proposed** (supports RTL8188EU, GPL-2.0-only, fw `rtlwifi/rtl8188eufw.bin`).
  Limitation: cut I unsupported.
- **Stage 2 in progress:** read-only `RTL8188EUProbe.kext` (`make usbprobe`, `make install`/`load`).
  Builds; **not yet confirmed on hardware**. `kextutil` first failed on dependency
  `IOUSBHostFamily 1.0` -> fixed to `1.2`; then macOS required approval + restart (`make install`).
  Next: user reboots, sends `log show` lines (`REG_SYS_CFG`, cut, endpoints). Then Stage 3.
- Old `RTW88USBDevice.cpp` in the clone is stale/unbuilt; do not build on it.

## Verified facts (from this session)

- User dongle: Realtek `0bda:8179` "802.11n NIC" (`ioreg -p IOUSB`; `system_profiler` printed nothing).
  Interface 0, class 0xFF, 3 endpoints. USB 2.0.
- macOS **26.7.1 Tahoe** (needs restored legacy IO80211 stack for the final Wi-Fi driver).
- Tahoe IOUSBHostFamily = 1.2 (compat 1.0.1). Match on `IOUSBHostInterface`; interface
  `deviceRequest()` has no `forClient` arg.
- Register access (rtl8xxxu): ctrl req 0x05, type 0xC0 read / 0x40 write, wValue=addr, wIndex=0, 500 ms.
  `REG_SYS_CFG`=0xF0, cut=(v&0xF000)>>12.

## Working conventions

- Commit messages in English, conventional prefixes `feat:`, `fix:`, `chore:` only.
- Chat with the user in Indonesian.

## Unverified — check before relying on it (items marked ✔ are resolved above)

- ✔ Whether upstream Linux `rtl8xxxu` covers RTL8188EU in current trees (my assumption: partially or not;
  RTL8188EU historically had out-of-tree `r8188eu` / `rtl8188eus`). **Verify first.**
- ✔ Exact USB VID:PID of the user's TP-Link dongle (get via `system_profiler SPUSBDataType` on macOS).
- Whether AirPort-RTW's frontend talks to the core through a narrow interface (ops table) or is
  entangled with rtw88 internals. This decides the whole design.
- Required firmware file for 8188EU (`rtl8188eufw.bin` is the expected name) and its license/redistribution terms.
- Target macOS version of the user's machine.

## Plan (do in order, one stage per reboot-test)

0. **Recon (no code):** read `src/` and map where the PCIe transport attaches to the rtw88 core,
   and where the IO80211 frontend calls into the core. Write findings to `docs/recon.md`.
   Identify the seam where a second core + USB transport can plug in.
1. **Pick the 8188EU core.** Candidates: Linux `rtl8xxxu` (mac80211-style, closest to the existing
   frontend contract) vs vendor `rtl8188eus` (self-contained HAL, messier). Decide with evidence
   from stage 0, and record the decision in `docs/decisions.md`.
2. **USB transport on macOS (IOUSBHost):** match by VID:PID, claim interface, find bulk IN/OUT
   endpoints, implement register read/write via vendor control requests, async bulk TX/RX with
   completions, safe teardown on hot-unplug.
3. **Core bring-up:** chip/version read, efuse (MAC address), firmware download, MAC/BB/RF init.
4. **Frontend hookup:** register the 8188EU core behind the same interface used by rtw88 so
   scan/assoc/TX/RX paths in AirPort-RTW are reused. 2.4 GHz, 1T1R, 11n only.
5. **Hardening:** hot-unplug, sleep/wake, reconnect, no conflict with the PCIe path.

Each stage needs a defined pass/fail test (see "Test protocol") before moving on.

## Test protocol

Order of validation, one step per reboot:
1. kext loads and attaches to the USB device (IORegistry shows it)
2. register read returns sane chip ID; MAC from efuse
3. firmware download succeeds
4. scan shows nearby 2.4 GHz networks
5. join WPA2 network, ping works
6. sleep/wake, unplug/replug

On any failure, capture: `log show --last 5m --predicate 'sender == "AirPortRTW"'`,
`kextstat | grep -i rtw`, and the panic report from `/Library/Logs/DiagnosticReports/` if any.
Do not iterate blindly; read the log first.

## Rules for working in this repo

- This is **kernel code**. A bug can panic the machine. Prefer small, reviewable changes.
- Always keep a bootable fallback EFI. Never tell the user to replace their only working EFI.
- Do not load the old Feixiao `rtw88.kext` together with AirPortRTW (both match the same PCI device).
  Do not load the chris1111 USB package while testing the new USB path.
- Do not invent register values, USB IDs, firmware names, or API behavior. If unsure, say so and
  cite the file/line or source you are working from. Mark guesses as guesses in code comments.
- Do not commit firmware blobs unless their license allows it; fetch them in `scripts/` like the
  existing bootstrap does.
- Keep the PCIe path working. Run the existing build (`make airport`) after every change and
  confirm it still compiles before touching hardware-facing behavior.
- Match the existing code style and file organization. Put new USB/8188EU code in clearly named
  files (e.g. `src/usb/`, `src/rtl8188eu/`) rather than editing rtw88 internals in place.
- Add a short note to `RELEASE_NOTES` / docs for any user-visible change, including the new
  supported device IDs and the OpenCore steps they need.

## Collaboration notes

- The user builds and tests on their own Hackintosh and reports logs back. Claude Code should make
  every change easy to test: say exactly what to build, what to copy, what to expect, and what to
  send back if it fails.
- When a stage is blocked on missing information (e.g. macOS version, dongle PID), ask for exactly
  that item rather than proceeding on assumptions.
