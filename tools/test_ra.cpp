// SPDX-License-Identifier: GPL-2.0-only
// Host test for src/usb/rtl8188eu_ra.h (no kernel, no hardware): bad reports must walk the rate down, clean ones up.
#include "rtl8188eu_ra.h"
#include <stdio.h>
using namespace rtl8188eu_ra;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint8_t item[8];
static void rpt(Info *ra, unsigned r0, unsigned r1, unsigned r2, unsigned r3, unsigned r4, unsigned drop) {
    item[0] = r0 & 0xff; item[1] = r0 >> 8; item[2] = r1; item[3] = r2; item[4] = r3; item[5] = r4; item[6] = drop; item[7] = 0;
    uint16_t t; handleItem(ra, item, &t);
}

int main() {
    Info ra; init(&ra, 0x0fff, 0x0b, false);
    CHECK(ra.highest_rate == 11 && ra.lowest_rate == 0 && ra.decision_rate == 11 && ra.rpt_time == 0x927c);
    // heavy retries at 54M: must drop within a few reports, and never leave [lowest, highest]
    ra.rssi_sta_ra = 60;
    for (int i = 0; i < 40; i++) { rpt(&ra, 0, 20, 20, 20, 20, 10); CHECK(ra.decision_rate <= 11); }
    printf("after bad link: rate idx %u\n", ra.decision_rate);
    CHECK(ra.decision_rate < 11);
    uint8_t low = ra.decision_rate;
    // clean link: many first-try successes must climb again
    for (int i = 0; i < 200; i++) rpt(&ra, 200, 0, 0, 0, 0, 0);
    printf("after clean link: rate idx %u\n", ra.decision_rate);
    CHECK(ra.decision_rate > low);
    CHECK(ra.decision_rate <= 11);
    // total == 0: nothing changes
    uint8_t keep = ra.decision_rate; rpt(&ra, 0, 0, 0, 0, 0, 0); CHECK(ra.decision_rate == keep);
    // rpt_time stays on the 6-entry table
    bool ok = false; for (int k = 0; k < 6; k++) if (kDynamicTxRptTiming[k] == ra.rpt_time) ok = true; CHECK(ok);
    // mask that excludes CCK: lowest must be 4 (6M) and rate never falls below it
    Info r2; init(&r2, 0x0ff0, 0x0b, false); CHECK(r2.lowest_rate == 4);
    r2.rssi_sta_ra = 40;
    for (int i = 0; i < 60; i++) { rpt(&r2, 0, 20, 20, 20, 20, 10); CHECK(r2.decision_rate >= 4); }
    // rtl8xxxu_refresh_rate_mask port: HT peer (MCS0-7) and legacy BG peer, three signal levels (snr = dBm + 100)
    Info h; init(&h, 0x0fff, 0, false);
    CHECK(refreshRateMask(&h, 60, 0xfff, 0xff, true, true));  CHECK(h.rate_mask == 0x000f0000 && h.rssi_level == 1 && h.lowest_rate == 16 && h.highest_rate == 19);
    CHECK(!refreshRateMask(&h, 60, 0xfff, 0xff, true, false));                        // same level, no force: unchanged
    CHECK(refreshRateMask(&h, 40, 0xfff, 0xff, true, false)); CHECK(h.rate_mask == 0x000ff000 && h.rssi_level == 2);
    CHECK(!refreshRateMask(&h, 54, 0xfff, 0xff, true, false));                        // hysteresis: MID needs snr > 55 to go HIGH
    CHECK(refreshRateMask(&h, 10, 0xfff, 0xff, true, false)); CHECK(h.rate_mask == 0x000ff005 && h.rssi_level == 3);
    h.decision_rate = h.pre_rate = 3; clampRate(&h); CHECK(h.decision_rate == 3);     // clamp is range-only like Linux (membership is handled by rateUp/rateDown)
    h.decision_rate = h.pre_rate = 30; clampRate(&h); CHECK(h.decision_rate == 19);
    Info g; init(&g, 0x0fff, 0, false);
    CHECK(refreshRateMask(&g, 60, 0xfff, 0, false, true)); CHECK(g.rate_mask == 0x00000f00 && g.highest_rate == 11 && g.lowest_rate == 8);
    CHECK(refreshRateMask(&g, 30, 0xfff, 0, false, false)); CHECK(g.rate_mask == 0x00000ff0);
    CHECK(refreshRateMask(&g, 5, 0xfff, 0, false, false)); CHECK(g.rate_mask == 0x00000ff5);
    g.decision_rate = g.pre_rate = 11; clampRate(&g); CHECK(g.decision_rate == 11);
    printf(fails ? "test_ra: FAILED (%d)\n" : "test_ra: OK\n", fails);
    return fails != 0;
}
