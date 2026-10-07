// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.cpp — see header. Register constants are from Linux
// drivers/net/wireless/realtek/rtl8xxxu/{rtl8xxxu.h,regs.h} (verified 2026-10-07).

#include "RTL8188EUProbe.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/usb/StandardUSB.h>

#define super IOService
OSDefineMetaClassAndStructors(RTL8188EUProbe, IOService)

#define LOGP "RTL8188EUProbe: "

enum {
    kRealtekUsbRead   = 0xC0,   // REALTEK_USB_READ
    kRealtekUsbWrite  = 0x40,   // REALTEK_USB_WRITE
    kRealtekUsbCmdReq = 0x05,   // REALTEK_USB_CMD_REQ
    kRegSysCfg        = 0x00F0, // REG_SYS_CFG
    kSysCfgChipVerMask  = 0xF000, // SYS_CFG_CHIP_VERSION_MASK (bits 12-15)
    kSysCfgTrpVauxEn    = 1u << 23, // SYS_CFG_TRP_VAUX_EN (test chip)
    kCtlTimeoutMs     = 500,    // RTW_USB_CONTROL_MSG_TIMEOUT

    // regs.h
    kRegSysIsoCtrl    = 0x0000, kSysIsoPwcEv12v = 1u << 15,
    kRegSysFunc       = 0x0002, kSysFuncEldr    = 1u << 12,
    kRegSysClkr       = 0x0008, kSysClkAna8m    = 1u << 1, kSysClkLoaderEnable = 1u << 5,
    kReg9346Cr        = 0x000A, kEepromBoot     = 1u << 4, kEepromEnable = 1u << 5,
    kRegEfuseCtrl     = 0x0030,
    kRegEfuseAccess   = 0x00CF, kEfuseAccessEnable = 0x69, kEfuseAccessDisable = 0x00,
    // rtl8xxxu.h
    kEfuseMapLen      = 512,    // EFUSE_MAP_LEN == EFUSE_REAL_CONTENT_LEN_8723A
    kEfuseMaxWordUnit = 4,
    kMaxRegPoll       = 500,    // RTL8XXXU_MAX_REG_POLL
    // struct rtl8188eu_efuse (rtl8xxxu.h)
    kEfuseRtlId       = 0x8129, kEfuseOffMac = 0xD7,
};

IOReturn RTL8188EUProbe::regRead(uint16_t addr, void *out, uint16_t len)
{
    if (!_iface || !_ctlBuf || len > 4) return kIOReturnNotReady;
    StandardUSB::DeviceRequest req = {};
    req.bmRequestType = kRealtekUsbRead;
    req.bRequest      = kRealtekUsbCmdReq;
    req.wValue        = addr;   // x86_64 only: host order == USB little-endian
    req.wIndex        = 0;
    req.wLength       = len;
    uint32_t got = 0;
    IOReturn r = _iface->deviceRequest(req, _ctlBuf, got, kCtlTimeoutMs);
    if (r != kIOReturnSuccess) return r;
    if (got != len) return kIOReturnUnderrun;
    bcopy(_ctlBuf->getBytesNoCopy(), out, len);
    return kIOReturnSuccess;
}

IOReturn RTL8188EUProbe::regWrite(uint16_t addr, const void *in, uint16_t len)
{
    if (!_iface || !_ctlBuf || len > 4) return kIOReturnNotReady;
    bcopy(in, _ctlBuf->getBytesNoCopy(), len);
    StandardUSB::DeviceRequest req = {};
    req.bmRequestType = kRealtekUsbWrite;
    req.bRequest      = kRealtekUsbCmdReq;
    req.wValue        = addr;
    req.wIndex        = 0;
    req.wLength       = len;
    uint32_t got = 0;
    IOReturn r = _iface->deviceRequest(req, _ctlBuf, got, kCtlTimeoutMs);
    if (r != kIOReturnSuccess) return r;
    return got == len ? kIOReturnSuccess : kIOReturnUnderrun;
}

// Port of rtl8xxxu_read_efuse8 (core.c): select offset, clear bit 31 of ctrl+3,
// poll ctrl bit 31 for completion, then data is in the low byte.
IOReturn RTL8188EUProbe::efuseRead8(uint16_t offset, uint8_t *data)
{
    uint8_t v8; uint32_t v32 = 0; IOReturn r;

    if ((r = write8(kRegEfuseCtrl + 1, offset & 0xff)) != kIOReturnSuccess) return r;
    if ((r = read8(kRegEfuseCtrl + 2, &v8)) != kIOReturnSuccess) return r;
    v8 = (v8 & 0xfc) | ((offset >> 8) & 0x03);
    if ((r = write8(kRegEfuseCtrl + 2, v8)) != kIOReturnSuccess) return r;

    if ((r = read8(kRegEfuseCtrl + 3, &v8)) != kIOReturnSuccess) return r;
    if ((r = write8(kRegEfuseCtrl + 3, v8 & 0x7f)) != kIOReturnSuccess) return r;

    int i;
    for (i = 0; i < kMaxRegPoll; i++) {
        if ((r = read32(kRegEfuseCtrl, &v32)) != kIOReturnSuccess) return r;
        if (v32 & (1u << 31)) break;
    }
    if (i == kMaxRegPoll) return kIOReturnTimeout;

    IODelay(50);
    if ((r = read32(kRegEfuseCtrl, &v32)) != kIOReturnSuccess) return r;
    *data = v32 & 0xff;
    return kIOReturnSuccess;
}

// Port of rtl8xxxu_read_efuse (core.c). is_multi_func branch omitted: it is not
// set for the 8188EU (assumption: not verified against 8188e.c fops).
// Writes only the efuse access/power/clock enables, exactly as Linux does before power-on.
IOReturn RTL8188EUProbe::efuseReadAll()
{
    IOReturn r;
    uint16_t v16;

    if ((r = read16(kReg9346Cr, &v16)) != kIOReturnSuccess) return r;
    IOLog(LOGP "9346CR=0x%04x boot=%s%s\n", v16, (v16 & kEepromBoot) ? "EEPROM" : "EFUSE",
          (v16 & kEepromEnable) ? " (EEPROM present)" : "");

    memset(_efuse, 0xff, sizeof(_efuse));
    if ((r = write8(kRegEfuseAccess, kEfuseAccessEnable)) != kIOReturnSuccess) return r;

    do {
        if ((r = read16(kRegSysIsoCtrl, &v16)) != kIOReturnSuccess) break;
        if (!(v16 & kSysIsoPwcEv12v) &&
            (r = write16(kRegSysIsoCtrl, v16 | kSysIsoPwcEv12v)) != kIOReturnSuccess) break;

        if ((r = read16(kRegSysFunc, &v16)) != kIOReturnSuccess) break;
        if (!(v16 & kSysFuncEldr) &&
            (r = write16(kRegSysFunc, v16 | kSysFuncEldr)) != kIOReturnSuccess) break;

        if ((r = read16(kRegSysClkr, &v16)) != kIOReturnSuccess) break;
        uint16_t need = kSysClkLoaderEnable | kSysClkAna8m;
        if ((v16 & need) != need &&
            (r = write16(kRegSysClkr, v16 | need)) != kIOReturnSuccess) break;

        uint16_t addr = 0;
        while (addr < kEfuseMapLen) {
            uint8_t header, ext, val8;
            uint16_t offset, mask;
            if ((r = efuseRead8(addr++, &header)) != kIOReturnSuccess) break;
            if (header == 0xff) break;
            if ((header & 0x1f) == 0x0f) {              // extended header
                offset = (header & 0xe0) >> 5;
                if ((r = efuseRead8(addr++, &ext)) != kIOReturnSuccess) break;
                if ((ext & 0x0f) == 0x0f) continue;     // all words disabled
                offset |= (ext & 0xf0) >> 1;
                mask = ext & 0x0f;
            } else {
                offset = (header >> 4) & 0x0f;
                mask = header & 0x0f;
            }
            uint16_t map = offset * 8;
            for (int i = 0; i < kEfuseMaxWordUnit; i++) {
                if (mask & (1u << i)) { map += 2; continue; }
                if ((r = efuseRead8(addr++, &val8)) != kIOReturnSuccess) break;
                if (map >= kEfuseMapLen - 1) { IOLog(LOGP "illegal efuse map_addr 0x%04x\n", map); r = kIOReturnBadArgument; break; }
                _efuse[map++] = val8;
                if ((r = efuseRead8(addr++, &val8)) != kIOReturnSuccess) break;
                _efuse[map++] = val8;
            }
            if (r != kIOReturnSuccess) break;
        }
    } while (0);

    write8(kRegEfuseAccess, kEfuseAccessDisable);   // always restore
    return r;
}

void RTL8188EUProbe::logEfuse()
{
    uint16_t id = (uint16_t)(_efuse[0] | (_efuse[1] << 8));
    const uint8_t *m = &_efuse[kEfuseOffMac];
    IOLog(LOGP "efuse rtl_id=0x%04x (%s, expect 0x%04x)\n", id,
          id == kEfuseRtlId ? "OK" : "MISMATCH", kEfuseRtlId);
    IOLog(LOGP "efuse MAC %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    for (int row = 0; row < 16; row++) {
        const uint8_t *b = &_efuse[row * 16];
        IOLog(LOGP "efuse[%03x] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
              row * 16, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
              b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    }
}

bool RTL8188EUProbe::init(OSDictionary *dict)
{
    IOLog(LOGP "init (personality matched)\n");
    return super::init(dict);
}

IOService *RTL8188EUProbe::probe(IOService *provider, SInt32 *score)
{
    IOLog(LOGP "probe provider=%s\n", provider ? provider->getName() : "(null)");
    return super::probe(provider, score);
}

bool RTL8188EUProbe::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    IOLog(LOGP "start provider class=%s\n", provider->getMetaClass()->getClassName());
    _iface = OSDynamicCast(IOUSBHostInterface, provider);
    if (!_iface) { IOLog(LOGP "provider is not IOUSBHostInterface\n"); return false; }

    if (!_iface->open(this)) { IOLog(LOGP "open interface failed\n"); return false; }
    _open = true;
    _iface->retain();

    const StandardUSB::InterfaceDescriptor *id = _iface->getInterfaceDescriptor();
    IOLog(LOGP "attached: interface %u class 0x%02x endpoints %u\n",
          id->bInterfaceNumber, id->bInterfaceClass, id->bNumEndpoints);

    // Enumerate endpoints (log only; pipes kept for Stage 2 TX/RX work).
    const StandardUSB::EndpointDescriptor *ep = nullptr;
    while ((ep = (const StandardUSB::EndpointDescriptor *)
            StandardUSB::getNextAssociatedDescriptorWithType(
                _iface->getConfigurationDescriptor(), id,
                (const StandardUSB::Descriptor *)ep, kDescriptorTypeEndpoint)) != nullptr) {
        uint8_t addr = StandardUSB::getEndpointAddress(ep);
        uint8_t dir  = StandardUSB::getEndpointDirection(ep);
        uint8_t type = StandardUSB::getEndpointType(ep);
        IOLog(LOGP "endpoint 0x%02x dir=%s type=%u\n", addr,
              dir == kIOUSBEndpointDirectionIn ? "IN" : "OUT", type);
        if (type != kIOUSBEndpointTypeBulk) continue;
        if (dir == kIOUSBEndpointDirectionIn && !_bulkIn) {
            _bulkIn = _iface->copyPipe(addr);
        } else if (dir == kIOUSBEndpointDirectionOut && _nBulkOut < 4) {
            _bulkOut[_nBulkOut] = _iface->copyPipe(addr);
            if (_bulkOut[_nBulkOut]) _nBulkOut++;
        }
    }
    IOLog(LOGP "bulk IN %s, bulk OUT count %u\n", _bulkIn ? "ok" : "MISSING", _nBulkOut);

    _ctlBuf = IOBufferMemoryDescriptor::inTaskWithOptions(
        kernel_task, kIODirectionInOut, 4, 4);
    if (!_ctlBuf) { closeAll(); return false; }

    // Step 2 of the test protocol: read-only chip identification.
    uint32_t sysCfg = 0;
    IOReturn r = regRead(kRegSysCfg, &sysCfg, sizeof(sysCfg));
    if (r != kIOReturnSuccess) {
        IOLog(LOGP "SYS_CFG read failed: 0x%08x\n", r);
    } else {
        uint32_t cut = (sysCfg & kSysCfgChipVerMask) >> 12;
        IOLog(LOGP "REG_SYS_CFG(0x00f0) = 0x%08x cut=%u%s%s\n", sysCfg, cut,
              (sysCfg & kSysCfgTrpVauxEn) ? " TEST-CHIP(unsupported by rtl8xxxu)" : "",
              cut == 8 ? " CUT-I(unsupported by rtl8xxxu)" : "");
        // Test protocol step 2b: MAC from efuse. Skip on chips rtl8xxxu rejects.
        if (!(sysCfg & kSysCfgTrpVauxEn) && cut != 8) {
            IOReturn er = efuseReadAll();
            if (er != kIOReturnSuccess) IOLog(LOGP "efuse read failed: 0x%08x\n", er);
            else logEfuse();
        }
    }

    registerService();
    return true;
}

bool RTL8188EUProbe::willTerminate(IOService *provider, IOOptionBits options)
{
    // Hot-unplug: end the session so the port can re-enumerate.
    closeAll();
    return super::willTerminate(provider, options);
}

void RTL8188EUProbe::closeAll()
{
    if (_bulkIn) { _bulkIn->abort(); _bulkIn->release(); _bulkIn = nullptr; }
    for (uint32_t i = 0; i < _nBulkOut; i++) {
        if (_bulkOut[i]) { _bulkOut[i]->abort(); _bulkOut[i]->release(); _bulkOut[i] = nullptr; }
    }
    _nBulkOut = 0;
    if (_ctlBuf) { _ctlBuf->release(); _ctlBuf = nullptr; }
    if (_iface) {
        if (_open) { _iface->close(this); _open = false; }
        _iface->release();
        _iface = nullptr;
    }
}

void RTL8188EUProbe::stop(IOService *provider)
{
    closeAll();
    super::stop(provider);
}

void RTL8188EUProbe::free()
{
    closeAll();
    super::free();
}
