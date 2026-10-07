// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.cpp — see header.
#include "RTL8188EUProbe.hpp"
#include "RTL8188EUCore.hpp"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(RTL8188EUProbe, IOService)

#define LOGP "RTL8188EUProbe: "

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

    _core = OSTypeAlloc(RTL8188EUCore);
    if (!_core || !_core->init()) { IOLog(LOGP "core alloc failed\n"); OSSafeReleaseNULL(_core); return false; }
    if (!_core->attach(this, OSDynamicCast(IOUSBHostInterface, provider))) { OSSafeReleaseNULL(_core); return false; }
    _core->bringUp();

    registerService();
    return true;
}

bool RTL8188EUProbe::willTerminate(IOService *provider, IOOptionBits options)
{
    // Hot-unplug: end the session so the port can re-enumerate.
    if (_core) _core->closeAll();
    return super::willTerminate(provider, options);
}

void RTL8188EUProbe::stop(IOService *provider)
{
    if (_core) _core->closeAll();
    super::stop(provider);
}

void RTL8188EUProbe::free()
{
    OSSafeReleaseNULL(_core);
    super::free();
}
