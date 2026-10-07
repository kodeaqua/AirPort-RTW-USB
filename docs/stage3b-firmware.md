# Stage 3b — firmware download (RTL8188EU)

Source read 2026-10-07: `torvalds/linux` master, `drivers/net/wireless/realtek/rtl8xxxu/`
(`core.c`, `8188e.c`, `regs.h`, `rtl8xxxu.h`). Line numbers refer to that snapshot.
Nothing below is from memory; items marked **(open)** are not yet resolved.

## Firmware file

- Name: `rtlwifi/rtl8188eufw.bin` (`8188e.c`, `rtl8188eu_load_firmware`). linux-firmware WHENCE lists it
  as version 28.0, taken from Realtek driver v5.2.2.4_25483.20171222.
- License: the WHENCE group containing this entry ends with
  `Licence: Redistributable. See LICENCE.rtlwifi_firmware.txt`. **(open)** the license file was not found at the root or `rtlwifi/` paths I tried on git.kernel.org; locate and read it
  before deciding on embedding/redistribution. Until then: fetch in `scripts/`, never commit the blob.
- Layout: 32-byte header (`struct rtl8xxxu_firmware_header`: signature u16, category, function,
  major u16, minor u8, reserved, month/date/hour/minute, ramcodesize u16, reserved u16, svn_idx u32,
  3 x reserved u32), then payload. Payload size = `file_size - 32`. Only the payload is written to the chip.
- Signature check: `(sig & 0xfff0)` must be one of 92e0 92c0 88e0 88c0 5300 2300 88f0 10b0 92f0.

## Download (`rtl8xxxu_download_firmware`, core.c:2004)

Constants: `REG_MCU_FW_DL`=0x0080, `REG_FW_START_ADDRESS`=0x1000, `RTL_FW_PAGE_SIZE`=4096,
`REG_SYS_FUNC`=0x0002, `SYS_FUNC_CPU_ENABLE`=BIT(10). FW_DL bits: ENABLE=BIT0, READY=BIT1,
CSUM_REPORT=BIT2, WINT_INIT_READY=BIT6, RAM_SEL=BIT7.

1. `SYS_FUNC+1 |= 4` (8-bit); `SYS_FUNC |= CPU_ENABLE` (16-bit).
2. If `FW_DL` (8-bit) has RAM_SEL: write8 `FW_DL`=0, `reset_8051`.
3. `FW_DL` (8-bit) |= ENABLE.
4. `FW_DL` (32-bit) &= ~BIT(19).
5. `FW_DL` (8-bit) |= CSUM_REPORT (resets checksum).
6. For each 4096-byte page i: `FW_DL+2` (8-bit) = (read & 0xF8) | i; then writeN(0x1000, page).
   Last partial page (remainder) is written the same way with the next i.
7. Always (also on error): `FW_DL` (16-bit) &= ~ENABLE.
8. On `-EAGAIN` the caller retries up to 6 times in total (core.c:4003).

`writeN` (core.c): splits into chunks of `writeN_block_size` = **196** for 8188eu (`8188e.c:1863`),
each a control write (req 0x05, 0x40, wValue=addr advancing per chunk, wIndex=0). Short/failed chunk -> -EAGAIN.

## Start (`rtl8xxxu_start_firmware`, core.c:1944)

1. Poll `FW_DL` (32-bit) for CSUM_REPORT, max 1000 reads (no delay). Timeout -> -EAGAIN.
2. `FW_DL` (32-bit): |= READY, &= ~WINT_INIT_READY; write.
3. `reset_8051` (8188eu variant, `8188e.c:558`): read16 `SYS_FUNC`, clear CPU_ENABLE, write; set it again, write.
   (The 8188eu variant does NOT touch `REG_RSV_CTRL+1`; the generic `rtl8xxxu_reset_8051` does.)
4. Poll `FW_DL` (32-bit) for WINT_INIT_READY, max 1000 reads, `udelay(100)` each. Timeout -> "Firmware failed to start".
5. `init_reg_hmtfr` is not set for 8188eu in `rtl8188eu_fops` -> no HMTFR write.

## What Linux does BEFORE the download (core.c:3960-4010)

`power_on` (done, v0.3.0) -> `init_queue_reserved_page` (only if MAC was not already powered)
-> `init_queue_priority` -> write16 `REG_TRXFF_BNDY+2` = 0x25ff -> `request_hw_feature` -> download -> start.

- `init_queue_reserved_page` (core.c:3815): needs ep flags; 8188eu: total=0xa9, hi=0x29, lo=0x1c, norm=0x1c.
  Writes `REG_RQPN_NPQ`(0x214) then `REG_RQPN`(0x200) with LOAD.
- **(open)** Whether the firmware download truly needs the queue/RQPN/TRXFF_BNDY writes first on 8188eu
  (Linux order says do them; the dongle has 3 bulk OUT per CLAUDE.md, so `ep_tx_count` handling must be
  derived from the real endpoint list, see core.c:1699-1734). Plan: port them in the same order, do not skip.
- **(open)** `request_hw_feature` and `LLT init` ordering relative to the firmware: LLT is not on the
  path before download in the code above; confirm in `rtl8xxxu_init_device` before stage 3c.

## Mac-specific design questions

- A kext cannot read files from disk reliably. Options: embed the blob at build time (needs license OK),
  or load via a user-space helper. Decision pending license read.
- Control writes of 196 bytes per transfer, blob is 15262 bytes (measured from linux-firmware main) -> ~78 transfers; fine synchronously.
