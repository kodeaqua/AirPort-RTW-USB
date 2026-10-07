// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.hpp — Stage 2 probe: attach to the RTL8188EU USB interface,
// enumerate endpoints, chip ID and efuse (MAC) read. No firmware, no Wi-Fi.
#pragma once

struct Reg32Val;   // generated tables header, see RTL8188EUProbe.cpp

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

class RTL8188EUProbe : public IOService {
    OSDeclareDefaultStructors(RTL8188EUProbe)

public:
    bool init(OSDictionary *dict = nullptr) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    bool willTerminate(IOService *provider, IOOptionBits options) override;
    void free() override;

private:
    // Vendor control read, same protocol as Linux rtl8xxxu (core.c rtl8xxxu_read32):
    // bmRequestType 0xC0, bRequest 0x05, wValue = register address, wIndex = 0.
    IOReturn regRead(uint16_t addr, void *out, uint16_t len);
    // Vendor control write: bmRequestType 0x40, otherwise identical (rtl8xxxu_write8/16/32).
    IOReturn regWrite(uint16_t addr, const void *in, uint16_t len);
    IOReturn read8(uint16_t addr, uint8_t *v)   { return regRead(addr, v, 1); }
    IOReturn read16(uint16_t addr, uint16_t *v) { return regRead(addr, v, 2); }
    IOReturn read32(uint16_t addr, uint32_t *v) { return regRead(addr, v, 4); }
    IOReturn write8(uint16_t addr, uint8_t v)   { return regWrite(addr, &v, 1); }
    IOReturn write16(uint16_t addr, uint16_t v) { return regWrite(addr, &v, 2); }
    IOReturn write32(uint16_t addr, uint32_t v) { return regWrite(addr, &v, 4); }

    // Port of rtl8188eu_power_on (8188e.c). Stage 3b-1.
    IOReturn powerOn();

    // Stage 3b-2: firmware. Ports of rtl8xxxu core.c (see docs/stage3b-firmware.md).
    IOReturn regWriteN(uint16_t addr, const uint8_t *buf, uint32_t len);  // rtl8xxxu_writeN, 196-byte blocks
    IOReturn initQueues();       // init_queue_reserved_page + init_queue_priority (3 OUT eps) + TRXFF_BNDY
    IOReturn reset8051();        // rtl8188eu_reset_8051
    IOReturn fwDownload();       // rtl8xxxu_download_firmware
    IOReturn fwStart();          // rtl8xxxu_start_firmware
    IOReturn loadFirmware();     // header check + initQueues + download (6 tries) + start

    // Stage 3c-1: port of rtl8xxxu_init_mac (core.c:2187), see docs/stage3c-init.md.
    IOReturn initMac();

    // Stage 3c-2: RF path A serial access (core.c:867 read_rfreg, :912 write_rfreg) and
    // rtl8188eu_init_phy_bb (8188e.c:582). set_crystal_cap is deferred to 3c-5 (needs efuse xtal_k).
    IOReturn rfRead(uint8_t reg, uint32_t *out);
    IOReturn rfWrite(uint8_t reg, uint32_t data);
    IOReturn initPhyRegs(const Reg32Val *table);   // rtl8xxxu_init_phy_regs
    IOReturn initPhyBb();

    // Port of rtl8xxxu_read_efuse8 / rtl8xxxu_read_efuse (core.c). Fills _efuse[512].
    IOReturn efuseRead8(uint16_t offset, uint8_t *data);
    IOReturn efuseReadAll();
    void     logEfuse();
    void     closeAll();

    IOUSBHostInterface       *_iface  = nullptr;
    IOUSBHostPipe            *_bulkIn = nullptr;
    IOUSBHostPipe            *_bulkOut[4] = {};
    uint32_t                  _nBulkOut = 0;
    IOBufferMemoryDescriptor *_ctlBuf = nullptr;
    IOBufferMemoryDescriptor *_blkBuf = nullptr;   // 196 bytes, writeN chunk
    bool                      _open   = false;
    uint8_t                   _efuse[512];   // EFUSE_MAP_LEN, logical map, 0xff = unprogrammed
};
