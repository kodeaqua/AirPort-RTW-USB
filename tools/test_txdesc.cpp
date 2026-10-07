// Host test for src/usb/rtl8188eu_txdesc.h. Build: clang++ -std=c++17 -Isrc/usb tools/test_txdesc.cpp -o build/test_txdesc
#include "rtl8188eu_txdesc.h"
#include <stdio.h>
using namespace rtl8188eu_tx;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// Verbatim copy of the pre-refactor RTL8188EUProbe::fillTxDesc (hardware-proven mgmt path).
static void oldFill(uint8_t *d, const uint8_t *frame, uint16_t len, uint16_t seq)
{
    memset(d, 0, 32); memcpy(d + 32, frame, len);
    uint32_t dw0 = (1u << 7) | (1u << 3) | (1u << 2);
    if (frame[4] & 1) dw0 |= 1;
    le16(d, 0, len); d[2] = 32; d[3] = (uint8_t)dw0;
    le32(d, 4, (uint32_t)0x12 << 8);
    le32(d, 8, (1u << 16) | (1u << 24) | (1u << 25));
    le32(d, 12, (uint32_t)(seq & 0xfff) << 16);
    le32(d, 16, 1u << 8);
    le32(d, 20, (6u << 18) | (1u << 17));
    le16(d, 30, (1u << 29) >> 16);
    uint16_t c = 0; for (int i = 0; i < 16; i++) c ^= rd16(d, i * 2);
    le16(d, 28, c);
}

int main()
{
    // 1) mgmt frames are byte-identical to the old path (probe request bcast, auth unicast).
    uint8_t pr[40] = { 0x40, 0x00, 0, 0, 0xff,0xff,0xff,0xff,0xff,0xff, 0x50,0x3d,0xd1,0x6d,0x14,0x6a, 0xff,0xff,0xff,0xff,0xff,0xff, 0x10, 0x00, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    uint8_t au[30] = { 0xb0, 0x00, 0, 0, 0x11,0x22,0x33,0x44,0x55,0x66, 0x50,0x3d,0xd1,0x6d,0x14,0x6a, 0x11,0x22,0x33,0x44,0x55,0x66, 0x20, 0x00, 0,0,1,0,0,0 };
    for (int k = 0; k < 2; k++) {
        const uint8_t *f = k ? au : pr; uint16_t len = k ? sizeof au : sizeof pr; uint16_t seq = 0x123 + k;
        uint8_t a[256], b[256];
        Params p = { kQueueMgnt, 0, kSecNone, false, false, false, 0xff, seq };
        uint32_t n = build(a, f, len, p); oldFill(b, f, len, seq);
        CHECK(n == 32u + len); CHECK(memcmp(a, b, n) == 0);
    }

    // 2) QoS data, unicast, from-DS (to STA): addr1 = our MAC, no DS to-bit; CCMP, short preamble, 54M.
    uint8_t qd[40] = { 0x88, 0x02, 0,0, 0x50,0x3d,0xd1,0x6d,0x14,0x6a, 0xaa,0xbb,0xcc,0xdd,0xee,0xff, 0x11,0x22,0x33,0x44,0x55,0x66, 0x30, 0x04, 0x00,0x00 };
    uint8_t d[256];
    Params q = { selectQueue(qd, 2), kRate54M, kSecAes, true, false, false, 0xff, -1 };
    CHECK(q.queue == kQueueBe);
    build(d, qd, 26, q);
    CHECK(d[3] == (kDw0Own | kDw0Fs | kDw0Ls));                                   // not bmc (addr1 unicast, fromDS DA=addr1)
    uint32_t dw1 = rd16(d, 4) | (uint32_t)rd16(d, 6) << 16, dw3 = rd16(d, 12) | (uint32_t)rd16(d, 14) << 16;
    uint32_t dw4 = rd16(d, 16) | (uint32_t)rd16(d, 18) << 16, dw5 = rd16(d, 20) | (uint32_t)rd16(d, 22) << 16;
    CHECK(dw1 == (0u << 8 | kDw1SecAes));
    CHECK(dw3 == (0x043u << 16));                                                 // seq_ctrl 0x0430 >> 4 = 0x43
    CHECK(dw4 == (kDw4UseDriverRate | (5u << 15) | kDw4Qos | kDw4ShortPreamble));
    CHECK(dw5 == (kRate54M | 0x0001ff00));

    // 3) non-QoS data to multicast DA with to-DS (DA = addr3), RTS on: BMC set, rts rate 24M.
    uint8_t nd[30] = { 0x08, 0x01, 0,0, 0x11,0x22,0x33,0x44,0x55,0x66, 0x50,0x3d,0xd1,0x6d,0x14,0x6a, 0x01,0x00,0x5e,0x00,0x00,0x01, 0x10, 0x00 };
    Params n2 = { selectQueue(nd, 0), kRate6M, kSecNone, false, true, false, 0xff, -1 };
    CHECK(n2.queue == kQueueVo);
    build(d, nd, 24, n2);
    CHECK(d[3] & kDw0Bmc);
    dw4 = rd16(d, 16) | (uint32_t)rd16(d, 18) << 16;
    CHECK(dw4 == (kDw4UseDriverRate | (5u << 15) | kRate24M | kDw4RtsCts | kDw4HwRts));
    CHECK(!(dw4 & kDw4Qos));

    // 4) checksum: xor of all 16 words incl. csum field must be 0.
    uint16_t x = 0; for (int i = 0; i < 16; i++) x ^= rd16(d, i * 2);
    CHECK(x == 0);

    // 5) queue/pipe maps.
    CHECK(selectQueue(pr, 2) == kQueueMgnt);
    uint8_t bc[24] = { 0x80 }; CHECK(selectQueue(bc, 2) == kQueueBeacon);
    CHECK(acToQueue(0) == kQueueVo && acToQueue(1) == kQueueVi && acToQueue(2) == kQueueBe && acToQueue(3) == kQueueBk);
    CHECK(pipeForQueue(kQueueMgnt, 2) == 0 && pipeForQueue(kQueueVo, 2) == 0 && pipeForQueue(kQueueBe, 2) == 1 && pipeForQueue(kQueueBk, 2) == 1);
    CHECK(pipeForQueue(kQueueVi, 3) == 1 && pipeForQueue(kQueueBe, 3) == 2 && pipeForQueue(kQueueMgnt, 3) == 0);
    CHECK(pipeForQueue(kQueueBe, 1) == -1);

    printf(fails ? "%d FAILED\n" : "all txdesc checks passed\n", fails);
    return fails != 0;
}
