// SPDX-License-Identifier: GPL-2.0-only
// RTL8188EUProbe.hpp — Stage 2 probe: attach to the RTL8188EU USB interface,
// enumerate endpoints, chip ID and efuse (MAC) read. No firmware, no Wi-Fi.
#pragma once

struct Reg32Val;
struct RfVal;   // generated tables header, see RTL8188EUProbe.cpp

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

    // Stage 3c-3: rtl8xxxu_init_phy_rf (core.c:2433) with rtl8188eu_radioa_init_table, RF path A.
    IOReturn initPhyRf();
    IOReturn rfVerify(uint32_t *checked, uint32_t *mismatch, uint32_t *firstBadReg, uint32_t *firstBadGot);

    // Stage 3c-4: LLT + WMAC/EDCA/beacon block of rtl8xxxu_init_device (see docs/stage3c-init.md).
    IOReturn lltWrite(uint8_t address, uint8_t data);
    IOReturn initLlt();
    IOReturn initWmac();

    // Stage 3c-5: set_tx_power (8188f.c:358), LC calibration (core.c:3498) and the tail of init_device.
    IOReturn setTxPower(int channel, bool ht40);
    IOReturn setChannel(int channel);
    IOReturn rxScan();
    // Stage 4b: REG_MACID from efuse + management-frame TX (txdesc32, MGNT queue).
    IOReturn setMacAddr();
    // Stage 4c: link-layer registers (see RTL8188EUProbe.cpp). Untested on hardware; no H2C yet.
    IOReturn setLinkType(uint8_t type);
    IOReturn setBssid(const uint8_t *bssid);
    IOReturn stopTxBeacon();
    IOReturn addStationInterface();
    IOReturn setBasicRates(uint32_t rateCfg);
    IOReturn setShortPreamble(bool on);
    IOReturn setSlot(bool shortSlot, bool peerHt);
    IOReturn joinBss(uint16_t aid);
    IOReturn leaveBss();
    IOReturn setKeyCcmp(uint8_t keyidx, bool pairwise, const uint8_t *mac, const uint8_t *key16, uint8_t *hwIdx);
    IOReturn clearKey(uint8_t hwIdx);
    IOReturn h2cCmd4(uint32_t data);
    IOReturn reportConnect(uint8_t macid, bool connect);
    void     linkSelfTest();
    IOReturn txMgmt(IOBufferMemoryDescriptor *buf, const uint8_t *frame, uint16_t len, uint16_t seq);
    IOReturn fillTxDesc(uint8_t *d, const uint8_t *frame, uint16_t len, uint16_t seq);

    // Stage 4d: async engine. Callbacks run on the USB workloop; keep them short and non-blocking.
    typedef void (*RxCallback)(void *ctx, const uint8_t *frame, uint32_t len);       // one received 802.11 frame (no FCS)
    typedef void (*TxDoneCallback)(void *ctx, void *cookie, IOReturn status);
    struct RxInfo { const uint8_t *frame; uint32_t len; bool crcBad; bool c2h; };
    struct RxSlot { IOBufferMemoryDescriptor *buf; };
    struct TxSlot { IOBufferMemoryDescriptor *buf; void *cookie; };
    static bool parseRx(const uint8_t *b, uint32_t got, RxInfo *out);
    static void rxCompleteTramp(void *owner, void *param, IOReturn status, uint32_t bytes);
    static void txCompleteTramp(void *owner, void *param, IOReturn status, uint32_t bytes);
    IOReturn asyncStart();      // allocate buffers
    void     asyncStop();       // stop RX, drain TX, free buffers
    IOReturn rxStart();
    void     rxStop();
    IOReturn txSubmitMgmt(const uint8_t *frame, uint16_t len, uint16_t seq, void *cookie);
    void     asyncSelfTest();
    IOReturn phyLcCalibrate();
    IOReturn initTail();

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
    uint8_t                   _nextMbox = 0;
    bool                      _asyncUp = false;
    RxSlot                    _rx[4] = {};
    TxSlot                    _tx[8] = {};
    volatile SInt32           _rxOutstanding = 0, _rxRunning = 0, _rxConsecErr = 0;
    volatile UInt32           _txBusyMask = 0;
    volatile UInt32           _stRxSubmitted = 0, _stRxCompleted = 0, _stRxBuffers = 0, _stRxFrames = 0, _stRxCrcBad = 0,
                              _stRxC2h = 0, _stRxErrors = 0, _stTxSubmitted = 0, _stTxCompleted = 0, _stTxErrors = 0;
    RxCallback                _rxCb = nullptr;
    TxDoneCallback            _txDoneCb = nullptr;
    void                     *_cbCtx = nullptr;
    uint32_t                  _camMap = 0;   // used security CAM entries
    uint8_t                   _efuse[512];   // EFUSE_MAP_LEN, logical map, 0xff = unprogrammed
};
