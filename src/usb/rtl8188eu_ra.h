// SPDX-License-Identifier: GPL-2.0-only
// rtl8188eu_ra.h — pure (no IOKit) port of the RTL8188EU software rate adaptation from rtl8xxxu 8188e.c
// (Linux torvalds/linux master, read 2026-10-08): tables, rtl8188e_rate_decision/_down/_up, power-training state,
// rtl8188e_handle_ra_tx_report2 (the per-item part) and rtl8188e_ra_info_init_all / arfb_refresh.
// Kept free of kernel headers so tools/test_ra.cpp can check it on the host.
//
// Differences from Linux (deliberate):
//  - rate mask is legacy only (bits 0..11), no HT/SGI; the caller supplies it.
//  - the REG_TX_REPORT_TIME write is not done here: handleReport() returns the new value and the core writes it later
//    from a context that may sleep (the report arrives in the USB completion).
//  - chip cut I tables are selected by the `cutI` argument (this dongle is cut D).
#pragma once
#include <stdint.h>
#include <string.h>

namespace rtl8188eu_ra {

enum { kRateSize = 28, kPerEntry = 23, kRetrySize = 5, kItemSize = 8,
       kDescRate54M = 0x0b, kDescRate11M = 0x03, kDescRate48M = 0x0a, kDescRateMcs7 = 0x13, kDescRateMcs5 = 0x11 };

static const uint8_t kRetryPenalty[kPerEntry][kRetrySize + 1] = {
    {5, 4, 3, 2, 0, 3}, {6, 5, 4, 3, 0, 4}, {6, 5, 4, 2, 0, 4}, {8, 7, 6, 4, 0, 6},
    {10, 9, 8, 6, 0, 8}, {10, 9, 8, 4, 0, 8}, {10, 9, 8, 2, 0, 8}, {10, 9, 8, 0, 0, 8},
    {18, 17, 16, 8, 0, 16}, {26, 25, 24, 16, 0, 24}, {34, 33, 32, 24, 0, 32}, {34, 31, 28, 20, 0, 32},
    {34, 31, 27, 18, 0, 32}, {34, 31, 26, 16, 0, 32}, {34, 30, 22, 16, 0, 32}, {34, 30, 24, 16, 0, 32},
    {49, 46, 40, 16, 0, 48}, {49, 45, 32, 0, 0, 48}, {49, 45, 22, 18, 0, 48}, {49, 40, 24, 16, 0, 48},
    {49, 32, 18, 12, 0, 48}, {49, 22, 18, 14, 0, 48}, {49, 16, 16, 0, 0, 48}
};
static const uint8_t kPtPenalty[kRetrySize + 1] = {34, 31, 30, 24, 0, 32};

static const uint8_t kRetryPenaltyIdxNormal[2][kRateSize] = {
    { 4, 4, 4, 5,  4, 4, 5, 7, 7, 7, 8, 0x0a,  4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,  5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f },
    { 0x0a, 0x0a, 0x0b, 0x0c,  0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,
      0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x13,  9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13 }
};
static const uint8_t kRetryPenaltyIdxCutI[2][kRateSize] = {
    { 4, 4, 4, 5,  4, 4, 5, 7, 7, 7, 8, 0x0a,  4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,  5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f },
    { 0x0a, 0x0a, 0x0b, 0x0c,  0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,
      0x06, 0x07, 0x08, 0x0d, 0x0e, 0x11, 0x11, 0x11,  9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13 }
};
static const uint8_t kRetryPenaltyUpIdxNormal[kRateSize] = {
    0x0c, 0x0d, 0x0d, 0x0f,  0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,
    0x0f, 0x10, 0x10, 0x12, 0x12, 0x13, 0x14, 0x15,  0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
};
static const uint8_t kRetryPenaltyUpIdxCutI[kRateSize] = {
    0x0c, 0x0d, 0x0d, 0x0f,  0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,
    0x0b, 0x0b, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12,  0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
};
static const uint8_t kRssiThreshold[kRateSize] = {
    0, 0, 0, 0,  0, 0, 0, 0, 0, 0x24, 0x26, 0x2a,  0x18, 0x1a, 0x1d, 0x1f, 0x21, 0x27, 0x29, 0x2a,  0, 0, 0, 0x1f, 0x23, 0x28, 0x2a, 0x2c
};
static const uint16_t kNThresholdHigh[kRateSize] = {
    4, 4, 8, 16,  24, 36, 48, 72, 96, 144, 192, 216,  60, 80, 100, 160, 240, 400, 600, 800,  300, 320, 480, 720, 1000, 1200, 1600, 2000
};
static const uint16_t kNThresholdLow[kRateSize] = {
    2, 2, 4, 8,  12, 18, 24, 36, 48, 72, 96, 108,  30, 40, 50, 80, 120, 200, 300, 400,  150, 160, 240, 360, 500, 600, 800, 1000
};
static const uint8_t kDroppingNecessary[kRateSize] = {
    1, 1, 1, 1,  1, 2, 3, 4, 5, 6, 7, 8,  1, 2, 3, 4, 5, 6, 7, 8,  5, 6, 7, 8, 9, 10, 11, 12
};
static const uint8_t kPendingForRateUpFail[5] = {2, 10, 24, 40, 60};
static const uint16_t kDynamicTxRptTiming[6] = {0x186a, 0x30d4, 0x493e, 0x61a8, 0x7a12, 0x927c};   // 200 ms .. 1200 ms

enum Timing { kDefaultTiming = 0, kIncreaseTiming, kDecreaseTiming };

struct Info {                       // struct rtl8xxxu_ra_info
    uint8_t  rate_id;
    uint32_t rate_mask, ra_use_rate;
    uint8_t  rate_sgi, rssi_sta_ra, pre_rssi_sta_ra, sgi_enable, decision_rate, pre_rate, highest_rate, lowest_rate;
    uint32_t nsc_up, nsc_down, total;
    uint16_t retry[5], drop, rpt_time, pre_min_rpt_time;
    uint8_t  dynamic_tx_rpt_timing_counter, ra_waiting_counter, ra_pending_counter, ra_drop_after_down;
    uint8_t  pt_try_state, pt_stage, pt_stop_count, pt_pre_rate, pt_pre_rssi, pt_mode_ss, ra_stage, pt_smooth_factor;
    bool     cutI;
};

inline void arfbRefresh(Info *ra)   // rtl8188e_arfb_refresh
{
    ra->ra_use_rate = ra->rate_mask;
    ra->highest_rate = 0;
    if (ra->ra_use_rate) for (int i = kRateSize; i >= 0; i--) if (i < 32 && (ra->ra_use_rate & (1u << i))) { ra->highest_rate = (uint8_t)i; break; }
    ra->lowest_rate = 0;
    if (ra->ra_use_rate) for (int i = 0; i < kRateSize; i++) if (ra->ra_use_rate & (1u << i)) { ra->lowest_rate = (uint8_t)i; break; }
    if (ra->highest_rate > kDescRateMcs7) ra->pt_mode_ss = 3;
    else if (ra->highest_rate > kDescRate54M) ra->pt_mode_ss = 2;
    else if (ra->highest_rate > kDescRate11M) ra->pt_mode_ss = 1;
    else ra->pt_mode_ss = 0;
}

// rtl8188e_ra_info_init_all, then rtl8188e_update_rate_mask for the legacy mask. `startRate` replaces Linux' MCS7 start
// (GUESS: caller passes an RSSI-based legacy rate; Linux starts at the highest rate and walks down).
inline void init(Info *ra, uint32_t rateMask, uint8_t startRate, bool cutI)
{
    memset(ra, 0, sizeof(*ra));
    ra->cutI = cutI;
    ra->rate_mask = rateMask;
    arfbRefresh(ra);
    if (startRate > ra->highest_rate) startRate = ra->highest_rate;
    if (startRate < ra->lowest_rate) startRate = ra->lowest_rate;
    ra->decision_rate = ra->pre_rate = startRate;
    ra->nsc_down = ra->nsc_up = ((uint32_t)kNThresholdHigh[startRate] + kNThresholdLow[startRate]) / 2;
    ra->rpt_time = 0x927c;
    ra->pt_stage = 5;
    ra->pt_smooth_factor = 192;
}

inline void setTxRptTiming(Info *ra, uint8_t timing)
{
    uint8_t idx;
    for (idx = 0; idx < 5; idx++) if (kDynamicTxRptTiming[idx] == ra->rpt_time) break;
    if (timing == kDefaultTiming) idx = 0;
    else if (timing == kIncreaseTiming) { if (idx < 5) idx++; }
    else if (timing == kDecreaseTiming) { if (idx > 0) idx--; }
    ra->rpt_time = kDynamicTxRptTiming[idx];
}

inline void rateDown(Info *ra)
{
    uint8_t rate_id = ra->pre_rate, lowest = ra->lowest_rate, highest = ra->highest_rate;
    if (rate_id > highest) {
        rate_id = highest;
    } else if (ra->rate_sgi) {
        ra->rate_sgi = 0;
    } else if (rate_id > lowest) {
        if (rate_id > 0) {
            for (int i = rate_id - 1; i >= lowest; i--) {
                if (ra->ra_use_rate & (1u << i)) { rate_id = (uint8_t)i; goto finish; }
            }
        }
    } else if (rate_id <= lowest) {
        rate_id = lowest;
    }
finish:
    if (ra->ra_waiting_counter == 1) { ra->ra_waiting_counter++; ra->ra_pending_counter++; }
    else if (ra->ra_waiting_counter > 1) { ra->ra_waiting_counter = 0; ra->ra_pending_counter = 0; }
    if (ra->ra_pending_counter >= 4) ra->ra_pending_counter = 4;
    ra->ra_drop_after_down = 1;
    ra->decision_rate = rate_id;
    setTxRptTiming(ra, kDecreaseTiming);
}

inline void rateUp(Info *ra)
{
    uint8_t rate_id = ra->pre_rate, highest = ra->highest_rate;
    if (ra->ra_waiting_counter == 1) {
        ra->ra_waiting_counter = 0; ra->ra_pending_counter = 0;
    } else if (ra->ra_waiting_counter > 1) {
        ra->pre_rssi_sta_ra = ra->rssi_sta_ra;
        goto finish;
    }
    setTxRptTiming(ra, kDefaultTiming);
    if (rate_id < highest) {
        for (unsigned i = rate_id + 1u; i <= highest; i++) {
            if (ra->ra_use_rate & (1u << i)) { rate_id = (uint8_t)i; goto finish; }
        }
    } else if (rate_id == highest) {
        if (ra->sgi_enable && !ra->rate_sgi) ra->rate_sgi = 1;
        else if (!ra->sgi_enable) ra->rate_sgi = 0;
    } else {
        rate_id = highest;
    }
finish:
    if (ra->ra_waiting_counter == (4 + kPendingForRateUpFail[ra->ra_pending_counter])) ra->ra_waiting_counter = 0;
    else ra->ra_waiting_counter++;
    ra->decision_rate = rate_id;
}

inline void resetCounter(Info *ra)
{
    uint8_t r = ra->decision_rate;
    ra->nsc_up = ra->nsc_down = ((uint32_t)kNThresholdHigh[r] + kNThresholdLow[r]) >> 1;
}

inline void rateDecision(Info *ra)
{
    if (ra->total == 0) return;
    if (ra->ra_drop_after_down) { ra->ra_drop_after_down--; resetCounter(ra); return; }

    const uint8_t *idx0 = ra->cutI ? kRetryPenaltyIdxCutI[0] : kRetryPenaltyIdxNormal[0];
    const uint8_t *idx1 = ra->cutI ? kRetryPenaltyIdxCutI[1] : kRetryPenaltyIdxNormal[1];
    const uint8_t *upIdx = ra->cutI ? kRetryPenaltyUpIdxCutI : kRetryPenaltyUpIdxNormal;

    if (ra->rssi_sta_ra < ra->pre_rssi_sta_ra - 3 || ra->rssi_sta_ra > ra->pre_rssi_sta_ra + 3) {
        ra->pre_rssi_sta_ra = ra->rssi_sta_ra;
        ra->ra_waiting_counter = 0;
        ra->ra_pending_counter = 0;
    }

    uint8_t rate_id = ra->pre_rate > ra->highest_rate ? ra->highest_rate : ra->pre_rate;
    uint8_t p1 = ra->rssi_sta_ra > kRssiThreshold[rate_id] ? idx0[rate_id] : idx1[rate_id];
    for (int i = 0; i < 5; i++) ra->nsc_down += (uint32_t)ra->retry[i] * kRetryPenalty[p1][i];
    if (ra->nsc_down > ra->total * kRetryPenalty[p1][5]) ra->nsc_down -= ra->total * kRetryPenalty[p1][5];
    else ra->nsc_down = 0;

    uint8_t p2 = upIdx[rate_id];
    for (int i = 0; i < 5; i++) ra->nsc_up += (uint32_t)ra->retry[i] * kRetryPenalty[p2][i];
    if (ra->nsc_up > ra->total * kRetryPenalty[p2][5]) ra->nsc_up -= ra->total * kRetryPenalty[p2][5];
    else ra->nsc_up = 0;

    if (ra->nsc_down < kNThresholdLow[rate_id] || ra->drop > kDroppingNecessary[rate_id]) rateDown(ra);
    else if (ra->nsc_up > kNThresholdHigh[rate_id]) rateUp(ra);

    if (ra->decision_rate == ra->pre_rate) ra->dynamic_tx_rpt_timing_counter++;
    else ra->dynamic_tx_rpt_timing_counter = 0;
    if (ra->dynamic_tx_rpt_timing_counter >= 4) { setTxRptTiming(ra, kIncreaseTiming); ra->dynamic_tx_rpt_timing_counter = 0; }

    ra->pre_rate = ra->decision_rate;
    resetCounter(ra);
}

inline void powerTrainingTryState(Info *ra)
{
    ra->pt_try_state = 0;
    switch (ra->pt_mode_ss) {
    case 3: if (ra->decision_rate >= 0x19) ra->pt_try_state = 1; break;            // DESC_RATE_MCS13 = MCS0 (0x0c) + 13
    case 2: if (ra->decision_rate >= kDescRateMcs5) ra->pt_try_state = 1; break;
    case 1: if (ra->decision_rate >= kDescRate48M) ra->pt_try_state = 1; break;
    case 0: if (ra->decision_rate >= kDescRate11M) ra->pt_try_state = 1; break;
    default: break;
    }
    if (ra->rssi_sta_ra < 48) {
        ra->pt_stage = 0;
    } else if (ra->pt_try_state == 1) {
        if (ra->pt_stop_count >= 10 || ra->pt_pre_rssi > ra->rssi_sta_ra + 5 || ra->pt_pre_rssi < ra->rssi_sta_ra - 5 ||
            ra->decision_rate != ra->pt_pre_rate) {
            if (ra->pt_stage == 0) ra->pt_stage = 1;
            else if (ra->pt_stage == 1) ra->pt_stage = 3;
            else ra->pt_stage = 5;
            ra->pt_pre_rssi = ra->rssi_sta_ra;
            ra->pt_stop_count = 0;
        } else {
            ra->ra_stage = 0;
            ra->pt_stop_count++;
        }
    } else {
        ra->pt_stage = 0;
        ra->ra_stage = 0;
    }
    ra->pt_pre_rate = ra->decision_rate;
    // Linux: `if (1)` ("TODO: false alarm statistics") => power training is always disabled.
    ra->pt_stage = 0; ra->ra_stage = 0; ra->pt_stop_count = 0;
}

inline void powerTrainingDecision(Info *ra)
{
    uint8_t temp_stage, stage_id, j;
    uint32_t numsc = 0, num_total = ra->total * kPtPenalty[5];
    for (j = 0; j <= 4; j++) {
        numsc += (uint32_t)ra->retry[j] * kPtPenalty[j];
        if (numsc > num_total) break;
    }
    j >>= 1;
    temp_stage = (uint8_t)((ra->pt_stage + 1) >> 1);
    stage_id = temp_stage > j ? (uint8_t)(temp_stage - j) : 0;
    ra->pt_smooth_factor = (uint8_t)((ra->pt_smooth_factor >> 1) + (ra->pt_smooth_factor >> 2) + stage_id * 16 + 2);
    if (ra->pt_smooth_factor > 192) ra->pt_smooth_factor = 192;
    stage_id = ra->pt_smooth_factor >> 6;
    temp_stage = (uint8_t)(stage_id * 2);
    if (temp_stage != 0) temp_stage--;
    if (ra->drop > 3) temp_stage = 0;
    ra->pt_stage = temp_stage;
}

// One TX report item for MACID 0 (rtl8188e_handle_ra_tx_report2, station case: items = 1). `rpt` = 8 bytes.
// Returns true if REG_TX_REPORT_TIME must be rewritten with *newRptTime.
inline bool handleItem(Info *ra, const uint8_t *rpt, uint16_t *newRptTime)
{
    ra->retry[0] = (uint16_t)(rpt[0] | (rpt[1] << 8));
    ra->retry[1] = rpt[2]; ra->retry[2] = rpt[3]; ra->retry[3] = rpt[4]; ra->retry[4] = rpt[5];
    ra->drop = rpt[6];
    ra->total = (uint32_t)ra->retry[0] + ra->retry[1] + ra->retry[2] + ra->retry[3] + ra->retry[4] + ra->drop;
    if (ra->total > 0) {
        if (ra->ra_stage < 5) rateDecision(ra);
        else if (ra->ra_stage == 5) powerTrainingTryState(ra);
        else powerTrainingDecision(ra);
        if (ra->ra_stage <= 5) ra->ra_stage++;
        else ra->ra_stage = 0;
    }
    uint16_t minRpt = 0x927c;
    if (minRpt > ra->rpt_time) minRpt = ra->rpt_time;
    if (minRpt != ra->pre_min_rpt_time) { ra->pre_min_rpt_time = minRpt; *newRptTime = minRpt; return true; }
    return false;
}

}  // namespace rtl8188eu_ra
