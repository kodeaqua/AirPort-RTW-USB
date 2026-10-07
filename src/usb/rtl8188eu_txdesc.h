// SPDX-License-Identifier: GPL-2.0-only
// rtl8188eu_txdesc.h — pure (no IOKit) builder for the 32-byte txdesc32 of the RTL8188EU.
// Port of rtl8xxxu core.c rtl8xxxu_tx (queue/key/qos selection), rtl8xxxu_fill_txdesc_v3 (8188e.c fops),
// rtl8xxxu_calc_tx_desc_csum and rtl8xxxu_queue_select (Linux torvalds/linux master, read 2026-10-07).
// Kept free of kernel headers so tools/test_txdesc.cpp can check it on the host.
//
// NOT ported: A-MPDU (AGG_ENABLE / ampdu density), HT40 bits, SGI, TX-report/rate adaptation.
// Linux takes the data rate from ra_info.decision_rate (starts at MCS7 and is adjusted from C2H TX reports).
// We have no RA yet, so the caller passes the descriptor rate explicitly (GUESS: pick a conservative
// basic rate until RA is ported; see docs/stage4-plan.md).
#pragma once
#include <stdint.h>
#include <string.h>

namespace rtl8188eu_tx {

enum {
    kDescSize = 32,
    // TXDESC_QUEUE_* (rtl8xxxu.h:494-503)
    kQueueBk = 0x2, kQueueBe = 0x0, kQueueVi = 0x5, kQueueVo = 0x7,
    kQueueBeacon = 0x10, kQueueHigh = 0x11, kQueueMgnt = 0x12, kQueueCmd = 0x13,
    // DESC_RATE_* (rtl8xxxu.h:431-464)
    kRate1M = 0x00, kRate2M = 0x01, kRate5_5M = 0x02, kRate11M = 0x03,
    kRate6M = 0x04, kRate9M = 0x05, kRate12M = 0x06, kRate18M = 0x07,
    kRate24M = 0x08, kRate36M = 0x09, kRate48M = 0x0a, kRate54M = 0x0b,
    kRateMcs0 = 0x0c, kRateMcs7 = 0x13,
    // txdw0 (byte at offset 3)
    kDw0Own = 1u << 7, kDw0Fs = 1u << 3, kDw0Ls = 1u << 2, kDw0Bmc = 1u << 0,
    // txdw1
    kDw1QueueShift = 8, kDw1EnDescId = 1u << 21, kDw1SecRc4 = 0x00400000, kDw1SecAes = 0x00c00000,
    // txdw2
    kDw2AggBreak = 1u << 16, kDw2AntA = 1u << 24, kDw2AntB = 1u << 25,
    // txdw3
    kDw3SeqShift = 16,
    // txdw4
    kDw4RtsRateShift = 0, kDw4Qos = 1u << 6, kDw4UseDriverRate = 1u << 8, kDw4CtsSelf = 1u << 11,
    kDw4RtsCts = 1u << 12, kDw4HwRts = 1u << 13, kDw4PtStageShift = 15, kDw4ShortPreamble = 1u << 24,
    // txdw5
    kDw5RetryLimitEnable = 1u << 17, kDw5RetryLimitShift = 18, kDw5DataFallback = 0x0001ff00,
    // txdw7 (le16 at offset 30)
    kDw7AntC16 = (1u << 29) >> 16,
    kPtStageInit = 5,   // rtl8188e_ra_info_init_all: pt_stage = 5
};

enum Sec { kSecNone = 0, kSecRc4 = 1, kSecAes = 2 };

struct Params {
    uint8_t  queue;          // kQueue* ; use selectQueue()
    uint8_t  rate;           // DESC_RATE_*; data frames only (mgmt always 1M like Linux v3 with rate=0)
    uint8_t  sec;            // enum Sec: hw_key cipher (RC4 = WEP/TKIP, AES = CCMP)
    bool     shortPreamble;  // vif->bss_conf.use_short_preamble
    bool     useRts;         // frame longer than rts_threshold
    bool     useCtsSelf;     // use_cts_prot
    uint8_t  macidKeyIdx;    // 0xff = none; hw_key_idx for broadcast with hw key (EN_DESC_ID, core.c:5538)
    int32_t  seqOverride;    // >= 0: use this 12-bit seq instead of the frame's seq_ctrl (probe kext mgmt path); -1 = from frame
    uint8_t  ptStage = kPtStageInit;   // ra_info.pt_stage (data frames, txdw4 bits 15+); rtl8188eu_ra keeps it
};

inline void le16(uint8_t *p, unsigned o, uint16_t v) { p[o] = (uint8_t)v; p[o + 1] = (uint8_t)(v >> 8); }
inline void le32(uint8_t *p, unsigned o, uint32_t v) { le16(p, o, (uint16_t)v); le16(p, o + 2, (uint16_t)(v >> 16)); }
inline uint16_t rd16(const uint8_t *p, unsigned o) { return (uint16_t)(p[o] | (p[o + 1] << 8)); }

// ieee80211 frame-control helpers (little-endian fc in the first two bytes of the frame).
inline bool isMgmt(const uint8_t *f)    { return (f[0] & 0x0c) == 0x00; }
inline bool isData(const uint8_t *f)    { return (f[0] & 0x0c) == 0x08; }
inline bool isBeacon(const uint8_t *f)  { return (f[0] & 0xfc) == 0x80; }
inline bool isQosData(const uint8_t *f) { return (f[0] & 0x8c) == 0x88; }
inline bool daIsGroup(const uint8_t *da) { return (da[0] & 1) != 0; }

// DA per ieee80211_get_DA(): to-DS frames carry DA in addr3, others in addr1.
inline const uint8_t *getDA(const uint8_t *f) { return (f[1] & 0x01) ? f + 16 : f + 4; }

// 802.11 AC (0=BE 1=BK 2=VI 3=VO, Linux IEEE80211_AC_* is VO=0 VI=1 BE=2 BK=3) -> TXDESC_QUEUE_*.
// We take the Linux enum value, as the frontend's skb queue mapping follows mac80211.
inline uint8_t acToQueue(unsigned linuxAc) {
    switch (linuxAc) { case 0: return kQueueVo; case 1: return kQueueVi; case 2: return kQueueBe; case 3: return kQueueBk; }
    return kQueueBe;
}
// rtl8xxxu_queue_select
inline uint8_t selectQueue(const uint8_t *f, unsigned linuxAc) {
    if (isBeacon(f)) return kQueueBeacon;
    if (isMgmt(f))   return kQueueMgnt;
    return acToQueue(linuxAc);
}

// Pipe (index into the bulk OUT list) for a TXDESC queue; init_queue_priority (core.c:2600-2710).
// 2 OUT eps: VO/VI/MGNT/HIGH/BEACON/CMD -> 0, BE/BK -> 1.
// 3 OUT eps: HIGH/MGNT/BEACON/CMD -> 0, VI/VO -> 1, BE/BK -> 2 (hip=hiq^3 = 0, vip=viq^3 = 1, vop=viq^3 = 1, bkp=bep=lo^3 = 2).
inline int pipeForQueue(uint8_t q, unsigned nBulkOut) {
    bool lowPrio = (q == kQueueBe || q == kQueueBk);
    if (nBulkOut == 2) return lowPrio ? 1 : 0;
    if (nBulkOut == 3) {
        if (lowPrio) return 2;
        if (q == kQueueVi || q == kQueueVo) return 1;
        return 0;
    }
    return -1;
}

// Build descriptor + payload copy. `d` must hold kDescSize + len bytes. Returns total length to submit.
// seq = 12-bit sequence number taken from the frame's seq_ctrl (IEEE80211_SEQ_TO_SN) by the caller helper below.
inline uint32_t build(uint8_t *d, const uint8_t *frame, uint16_t len, const Params &p) {
    memset(d, 0, kDescSize);
    memcpy(d + kDescSize, frame, len);

    uint32_t dw0 = kDw0Own | kDw0Fs | kDw0Ls;
    bool bmc = daIsGroup(getDA(frame));
    if (bmc) dw0 |= kDw0Bmc;
    le16(d, 0, len);                       // pkt_size
    d[2] = kDescSize;                      // pkt_offset
    d[3] = (uint8_t)dw0;                   // txdw0

    uint32_t dw1 = (uint32_t)p.queue << kDw1QueueShift;
    if (p.sec == kSecRc4) dw1 |= kDw1SecRc4;
    else if (p.sec == kSecAes) dw1 |= kDw1SecAes;
    if (p.sec != kSecNone && bmc && p.macidKeyIdx != 0xff) dw1 |= kDw1EnDescId;   // macid itself is unused by v3 fill
    le32(d, 4, dw1);

    // fill_txdesc_v3
    uint32_t dw2 = 0, dw4 = 0, dw5 = 0;
    uint32_t seq = p.seqOverride >= 0 ? (uint32_t)p.seqOverride & 0xfff
                                      : (uint32_t)(rd16(frame, 22) >> 4);   // IEEE80211_SEQ_TO_SN(seq_ctrl)
    bool rtsOn = p.useRts || p.useCtsSelf;
    uint32_t rtsRate = rtsOn ? kRate24M : 0;                  // ampdu path (not ported) would also use 24M

    if (isData(frame)) {
        dw5 = p.rate;
        dw4 |= kDw4UseDriverRate;
        dw4 |= (uint32_t)p.ptStage << kDw4PtStageShift;
        dw5 |= kDw5DataFallback;
    }
    dw2 |= kDw2AggBreak;                                      // no A-MPDU
    if (isMgmt(frame)) {
        dw5 = 0;                                              // rate 0 = 1M
        dw4 |= kDw4UseDriverRate;
        dw5 |= 6u << kDw5RetryLimitShift;
        dw5 |= kDw5RetryLimitEnable;
    }
    if (isQosData(frame)) dw4 |= kDw4Qos;
    if (p.shortPreamble) dw4 |= kDw4ShortPreamble;
    dw4 |= rtsRate << kDw4RtsRateShift;
    if (p.useRts)          dw4 |= kDw4RtsCts | kDw4HwRts;
    else if (p.useCtsSelf) dw4 |= kDw4CtsSelf | kDw4HwRts;
    dw2 |= kDw2AntA | kDw2AntB;

    le32(d, 8, dw2);
    le32(d, 12, seq << kDw3SeqShift);
    le32(d, 16, dw4);
    le32(d, 20, dw5);
    le16(d, 30, kDw7AntC16);

    uint16_t csum = 0;                                        // rtl8xxxu_calc_tx_desc_csum (csum field zero)
    for (int i = 0; i < kDescSize / 2; i++) csum ^= rd16(d, i * 2);
    le16(d, 28, csum);
    return kDescSize + len;
}

}  // namespace rtl8188eu_tx
