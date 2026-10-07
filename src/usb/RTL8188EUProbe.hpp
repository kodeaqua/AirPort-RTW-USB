// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.hpp — IOService that matches the RTL8188EU USB interface and owns an RTL8188EUCore.
// Used as a hardware regression harness: start() runs the full bring-up and self-tests and logs the results.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBHostInterface.h>

class RTL8188EUCore;

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
    RTL8188EUCore *_core = nullptr;
};
