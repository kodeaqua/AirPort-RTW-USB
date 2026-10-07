// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.hpp — Stage 2 probe: attach to the RTL8188EU USB interface,
// enumerate endpoints, read-only register access. No Wi-Fi functionality.
#pragma once

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
    void     closeAll();

    IOUSBHostInterface       *_iface  = nullptr;
    IOUSBHostPipe            *_bulkIn = nullptr;
    IOUSBHostPipe            *_bulkOut[4] = {};
    uint32_t                  _nBulkOut = 0;
    IOBufferMemoryDescriptor *_ctlBuf = nullptr;
    bool                      _open   = false;
};
