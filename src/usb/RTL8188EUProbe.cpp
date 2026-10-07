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
    kRealtekUsbCmdReq = 0x05,   // REALTEK_USB_CMD_REQ
    kRegSysCfg        = 0x00F0, // REG_SYS_CFG
    kSysCfgChipVerMask  = 0xF000, // SYS_CFG_CHIP_VERSION_MASK (bits 12-15)
    kSysCfgTrpVauxEn    = 1u << 23, // SYS_CFG_TRP_VAUX_EN (test chip)
    kCtlTimeoutMs     = 500,    // RTW_USB_CONTROL_MSG_TIMEOUT
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
    if (OSDynamicCast(IOUSBHostDevice, provider)) {
        // Diagnostic personality (see Info.plist): matched at device level; decline.
        IOLog(LOGP "matched at IOUSBHostDevice level (diagnostic), declining\n");
        return false;
    }

    _iface =OSDynamicCast(IOUSBHostInterface, provider);
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
