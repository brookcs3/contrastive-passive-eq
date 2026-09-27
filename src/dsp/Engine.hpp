// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// Real-time processing for one channel: a cascade of double-precision transposed direct-form II sections, a minimum-phase FIR
// correction, and click-free changes between settings.
//
// Every control on this unit is stepped, so there is no continuous parameter to ramp. A change of setting is made by designing the new
// filter, warming it up on the recent input (kWarmSeconds of history, so it starts near its steady state instead of from rest), and
// crossfading from the old filter to the new one over kFadeSeconds with a raised-cosine law. A change that arrives during a fade is held
// and applied when the fade ends (the newest one wins). Zero latency: nothing looks ahead.
#pragma once
#include <cmath>
#include <cstring>
#include "Design.hpp"

namespace cpeq {

static constexpr double kFadeSeconds = 0.015;
static constexpr double kWarmSeconds = 0.080;
static constexpr int kMaxHistory = 1 << 15;   // 32768 samples: covers kWarmSeconds up to 384 kHz

struct Cascade {
    DigitalDesign d;
    double s1[kMaxSections], s2[kMaxSections];
    double firLine[kFirTaps];
    int firPos = 0;

    void reset()
    {
        std::memset(s1, 0, sizeof(s1)); std::memset(s2, 0, sizeof(s2)); std::memset(firLine, 0, sizeof(firLine)); firPos = 0;
    }
    inline double tick(double x)
    {
        if (d.identity) return x;
        double y = d.gain * x;
        for (int i = 0; i < d.nsec; ++i) {
            const Biquad& q = d.sec[i];
            const double out = q.b0 * y + s1[i];
            s1[i] = q.b1 * y - q.a1 * out + s2[i];
            s2[i] = q.b2 * y - q.a2 * out;
            y = out;
        }
        firLine[firPos] = y;
        double acc = 0.0;
        int idx = firPos;
        for (int k = 0; k < kFirTaps; ++k) {
            acc += d.fir[k] * firLine[idx];
            idx = idx == 0 ? kFirTaps - 1 : idx - 1;
        }
        firPos = firPos + 1 == kFirTaps ? 0 : firPos + 1;
        return acc;
    }
};

class ChannelEngine {
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        fadeLen = int(std::lround(kFadeSeconds * fs)); if (fadeLen < 16) fadeLen = 16;
        warmLen = int(std::lround(kWarmSeconds * fs)); if (warmLen > kMaxHistory) warmLen = kMaxHistory;
        std::memset(history, 0, sizeof(history)); histPos = 0;
        ChannelSetting flat; flat.eqIn = false;
        active = &bank[0]; next = &bank[1];
        active->reset(); next->reset();
        designDigital(flat, fs, active->d, work);
        current = flat; fading = false; pendingValid = false; failed = false;
        ready = true; immediate = true;
    }

    // request a setting; bypass = unity (POWER off, or the channel's IN switch off)
    void setTarget(const ChannelSetting& cs)
    {
        if (!ready) return;
        if (immediate) {   // nothing has been heard since prepare(): apply at once, no fade (clean offline renders and impulse tests)
            DigitalDesign d;
            if (designDigital(cs, fs, d, work)) { active->d = d; active->reset(); current = cs; } else failed = true;
            return;
        }
        if (fading) { pending = cs; pendingValid = true; return; }
        if (cs == current) return;
        startFade(cs);
    }

    void process(const float* in, float* out, int n)
    {
        if (n > 0) immediate = false;
        for (int i = 0; i < n; ++i) {
            const double x = in[i];
            history[histPos] = x; histPos = (histPos + 1) & (kMaxHistory - 1);
            double y;
            if (fading) {
                const double ya = active->tick(x), yb = next->tick(x);
                const double t = double(fadePos) / fadeLen;
                const double w = 0.5 - 0.5 * std::cos(kPi * t);
                y = ya + w * (yb - ya);
                if (++fadePos >= fadeLen) {
                    Cascade* tmp = active; active = next; next = tmp;
                    fading = false;
                    if (pendingValid) { pendingValid = false; if (pending != current) startFade(pending); }
                }
            } else {
                y = active->tick(x);
            }
            out[i] = float(y);
        }
    }

    bool designFailed() const { return failed; }
    const DigitalDesign& activeDesign() const { return active->d; }

private:
    void startFade(const ChannelSetting& cs)
    {
        next->reset();
        if (!designDigital(cs, fs, next->d, work)) {   // never expected; keep the old filter rather than run a bad one
            failed = true; return;
        }
        // warm up on the recent input so the new filter starts close to its steady state
        if (!next->d.identity) {
            int pos = (histPos - warmLen) & (kMaxHistory - 1);
            for (int i = 0; i < warmLen; ++i) { next->tick(history[pos]); pos = (pos + 1) & (kMaxHistory - 1); }
        }
        current = cs; fading = true; fadePos = 0;
    }

    double fs = 48000.0;
    int fadeLen = 720, warmLen = 3840;
    Cascade bank[2];
    Cascade* active = &bank[0];
    Cascade* next = &bank[1];
    ChannelSetting current, pending;
    bool pendingValid = false, fading = false, ready = false, failed = false, immediate = true;
    int fadePos = 0;
    double history[kMaxHistory];
    int histPos = 0;
    DesignWork work;   // FFT plan and buffers for designDigital(), built with the engine (off the audio thread)
};

// flush denormals to zero for the duration of a process call (x86 SSE and AArch64)
struct DenormalGuard {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    unsigned int saved;
    DenormalGuard() { saved = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr(saved | 0x8040u); }
    ~DenormalGuard() { __builtin_ia32_ldmxcsr(saved); }
#elif defined(__aarch64__)
    unsigned long saved;
    DenormalGuard() { __asm__ __volatile__("mrs %0, fpcr" : "=r"(saved)); const unsigned long v = saved | (1ul << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }
    ~DenormalGuard() { __asm__ __volatile__("msr fpcr, %0" : : "r"(saved)); }
#else
    DenormalGuard() {}
#endif
};

} // namespace cpeq
