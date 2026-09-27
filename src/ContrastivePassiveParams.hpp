// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// The stepped front panel as plugin parameters: every knob and switch, per channel, with the panel's own positions. Names are
// written for a reader (human or agent): L_/R_ channel prefix, band1..band4 left to right on the panel.
#pragma once
#include <cstdio>
#include "dsp/Network.hpp"

namespace cpeq {

enum ParamKind { kPMode, kPType, kPGainStep, kPBandwidth, kPFreq, kPIn, kPTrim, kPLowPass, kPHighPass, kPLink, kPPower };

static constexpr int kPerBand = 5;
static constexpr int kPerChannel = kBands * kPerBand + 4;          // 24
static constexpr int kNumParams = 2 * kPerChannel + 2;             // 50: L, R, link, power
static constexpr int kParamLink = 2 * kPerChannel;
static constexpr int kParamPower = 2 * kPerChannel + 1;

struct ParamInfo { int kind; int channel; int band; };   // channel 0 = L, 1 = R, -1 global; band 0..3 or -1

inline ParamInfo paramInfo(int index)
{
    if (index == kParamLink) return { kPLink, -1, -1 };
    if (index == kParamPower) return { kPPower, -1, -1 };
    const int ch = index / kPerChannel, r = index % kPerChannel;
    if (r < kBands * kPerBand) return { kPMode + r % kPerBand, ch, r / kPerBand };
    return { kPIn + (r - kBands * kPerBand), ch, -1 };
}

inline int paramIndex(int kind, int channel, int band)
{
    if (kind == kPLink) return kParamLink;
    if (kind == kPPower) return kParamPower;
    if (kind <= kPFreq) return channel * kPerChannel + band * kPerBand + (kind - kPMode);
    return channel * kPerChannel + kBands * kPerBand + (kind - kPIn);
}

inline int paramSteps(int kind)   // number of positions
{
    switch (kind) {
    case kPMode: return 3;
    case kPType: return 2;
    case kPGainStep: return kGainSteps;
    case kPBandwidth: return kBandwidthSteps;
    case kPFreq: return kFreqPositions;
    case kPIn: return 2;
    case kPTrim: return kTrimPositions;
    case kPLowPass: return kLowPassPositions;
    case kPHighPass: return kHighPassPositions;
    case kPLink: return 2;
    case kPPower: return 2;
    }
    return 2;
}

inline int paramDefault(int kind)
{
    switch (kind) {
    case kPMode: return kOut;
    case kPType: return kBell;
    case kPGainStep: return 0;
    case kPBandwidth: return 7;
    case kPFreq: return 5;
    case kPIn: return 1;
    case kPTrim: return 5;
    case kPLowPass: return 0;
    case kPHighPass: return 0;
    case kPLink: return 0;
    case kPPower: return 1;
    }
    return 0;
}

// text for position v (the panel's own legends)
inline void positionLabel(int kind, int band, int v, char* out, int cap)
{
    static const char* modes[] = { "BOOST", "OUT", "CUT" };
    static const char* types[] = { "SHELF", "BELL" };
    static const char* lps[] = { "OFF", "52K", "40K", "27K", "20K", "15K" };
    static const char* hps[] = { "OFF", "12", "16", "23", "30", "39" };
    switch (kind) {
    case kPMode: std::snprintf(out, size_t(cap), "%s", modes[v]); return;
    case kPType: std::snprintf(out, size_t(cap), "%s", types[v]); return;
    case kPFreq: std::snprintf(out, size_t(cap), "%g", kBandFreqHz[band][v]); return;
    case kPTrim: std::snprintf(out, size_t(cap), "%+.1f", trimDb(v)); return;
    case kPLowPass: std::snprintf(out, size_t(cap), "%s", lps[v]); return;
    case kPHighPass: std::snprintf(out, size_t(cap), "%s", hps[v]); return;
    case kPIn: case kPLink: case kPPower: std::snprintf(out, size_t(cap), "%s", v ? "ON" : "OFF"); return;
    default: std::snprintf(out, size_t(cap), "%d", v); return;
    }
}

inline void paramName(int index, char* out, int cap)
{
    const ParamInfo pi = paramInfo(index);
    const char* ch = pi.channel == 0 ? "L" : "R";
    switch (pi.kind) {
    case kPMode: std::snprintf(out, size_t(cap), "%s_band%d_mode", ch, pi.band + 1); return;
    case kPType: std::snprintf(out, size_t(cap), "%s_band%d_type", ch, pi.band + 1); return;
    case kPGainStep: std::snprintf(out, size_t(cap), "%s_band%d_gain_step", ch, pi.band + 1); return;
    case kPBandwidth: std::snprintf(out, size_t(cap), "%s_band%d_bandwidth", ch, pi.band + 1); return;
    case kPFreq: std::snprintf(out, size_t(cap), "%s_band%d_freq_hz", ch, pi.band + 1); return;
    case kPIn: std::snprintf(out, size_t(cap), "%s_in", ch); return;
    case kPTrim: std::snprintf(out, size_t(cap), "%s_gain_db", ch); return;
    case kPLowPass: std::snprintf(out, size_t(cap), "%s_lowpass", ch); return;
    case kPHighPass: std::snprintf(out, size_t(cap), "%s_highpass", ch); return;
    case kPLink: std::snprintf(out, size_t(cap), "link"); return;
    case kPPower: std::snprintf(out, size_t(cap), "power"); return;
    }
}

// the unit is carried in the name (freq_hz, gain_db) and left out of the VST3 units field, so that hosts which append the unit to the
// name (Pedalboard does) keep the name as written
inline const char* paramUnit(int) { return ""; }

} // namespace cpeq
