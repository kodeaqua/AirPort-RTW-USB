# Building

Everything builds on macOS with Xcode Command Line Tools (tested on macOS 26.7.1, Xcode with a macOS 13+ SDK target).
The kext targets `x86_64`, minimum macOS 13.0 (`-mmacosx-version-min=13.0`).

## 1. Get the sources

```sh
git clone https://github.com/kodeaqua/AirPort-RTW-USB.git
cd AirPort-RTW-USB
git clone https://github.com/JoMei9019-real/AirPort-RTW.git AirPort-RTW      # gitignored: our changes live in patches/
cd AirPort-RTW && ./scripts/bootstrap-deps.sh && cd ..                         # MacKernelSDK, rtw88-stable, PCIe firmware
```

The upstream clone is deliberately **not** part of this repository. Everything we change in it is kept as patch files in
`patches/`, applied in order by `scripts/apply-usb-patches.sh` (it skips patches that are already applied and refuses to
continue if the clone no longer matches).

## 2. Build the integrated kext (PCIe + USB)

```sh
make airport-usb
```

This runs, in order: fetch `rtl8188eufw.bin` from linux-firmware into `build/fw/` and generate `rtl8188eu_fw.h`
(`scripts/fetch-rtl8188eu-fw.sh`), generate the MAC/BB/RF tables (`scripts/gen-rtl8188eu-tables.py`), apply
`patches/*.patch`, then build upstream `make airport` with `RTW_USB_SRC=src/usb`.

Output: `AirPort-RTW/build/out/AirPortRTW.kext`.

The PCIe-only build is unchanged: `make -C AirPort-RTW airport` still works. Run it after changes to confirm the PCIe
path still compiles.

## 3. Standalone probe kext (optional, for bring-up and debugging)

```sh
make usbprobe            # -> build/out/RTL8188EUProbe.kext
make load                # copy to a root-owned staging dir and kextutil it
make unload
make install             # persistent copy in /Library/Extensions (approve in Privacy & Security, reboot)
make uninstall
```

The probe kext runs the full bring-up with self-tests and logs the results. It does not create a Wi-Fi interface.

## 4. Host-side unit tests (no kernel, no hardware)

```sh
make test-txdesc         # TX descriptor builder
make test-ra             # software rate adaptation port
```

## Notes

- The firmware blob is Realtek-licensed: it is fetched at build time, never committed, and must not be redistributed.
- `build/` is gitignored. If a build looks stale, `make clean` removes the root `build/`; for the clone use
  `make -C AirPort-RTW clean`.
- A clone that was modified by hand makes `apply-usb-patches.sh` stop with an error instead of guessing.
  Restore it with `git -C AirPort-RTW checkout -- .` and run again.
