// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// From the analog zeros and poles to the digital filter the plugin runs.
//
// 1. Sections: poles and zeros are paired in the z-plane (nearest zeros to the most resonant poles first, like scipy's zpk2sos
//    'nearest'), giving second- and first-order sections.
// 2. Matched z-transform: every pole and zero maps exactly, z = exp(sT). Minimum phase (the analog network is minimum phase: every root
//    is in the open left half plane) and exact at low and middle frequencies; its magnitude error grows towards Nyquist.
// 3. Correction: a minimum-phase FIR (kFirTaps taps) built by cepstral folding of log(|H_analog| / |H_matched|) on kFftSize/2+1 bins
//    from DC to Nyquist. The total is minimum phase and follows the analog magnitude to within a few thousandths of a dB up to 20 kHz
//    (tests/test_dsp.cpp prints the figures). No latency: every stage is causal and minimum phase.
// Everything here is fixed-size and allocation-free once a DesignWork exists (its FFT plan is built when it is constructed), so it can
// run on the audio thread when a control moves.
#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include "Network.hpp"
#ifndef POCKETFFT_NO_MULTITHREADING
#define POCKETFFT_NO_MULTITHREADING   // one-dimensional transforms only: no thread pool
#endif
#include "../third_party/pocketfft/pocketfft_hdronly.h"

namespace cpeq {

static constexpr int kMaxSections = kMaxRoots;   // generous: at most one section per root
static constexpr int kFirTaps = 64;
static constexpr int kFftLog2 = 10;
static constexpr int kFftSize = 1 << kFftLog2;

struct Biquad { double b0, b1, b2, a1, a2; };

struct DigitalDesign {
    Biquad sec[kMaxSections];
    int nsec = 0;
    double gain = 1.0;
    double fir[kFirTaps];
    bool identity = true;   // no filtering at all (bypass, EQ out, or a flat setting)
};

// ------------------------------------------------------------------------------------------------ FFT (pocketfft)
// The transforms are pocketfft's (third_party/pocketfft, BSD-3-Clause; see its README.md). Convention: forward exp(-j 2 pi k n / N),
// unscaled; inverse exp(+j 2 pi k n / N), scaled by 1/N.
//
// pocketfft allocates in two places: when it builds a plan (factors and twiddles), and, upstream, a temporary work array on every
// transform. DesignWork keeps both out of the audio callback. It builds its plan once, in its constructor (a ChannelEngine and its
// DesignWork are constructed with the plugin, never in the audio callback), and it hands pocketfft a work array it owns, through the one
// local addition to the vendored header, cfftp::exec(c, fct, fwd, buf). So designDigital() does not allocate.
using FftPlan = pocketfft::detail::cfftp<double>;
using FftCplx = pocketfft::detail::cmplx<double>;
// pocketfft reads std::complex<T> arrays as its own cmplx<T> (its c2c() does), so the two layouts must agree
static_assert(sizeof(FftCplx) == sizeof(cplx) && alignof(FftCplx) == alignof(cplx), "complex layouts differ");

struct DesignWork {
    DesignWork() : plan(kFftSize) {}
    DesignWork(const DesignWork&) = delete;
    DesignWork& operator=(const DesignWork&) = delete;

    cplx buf[kFftSize];   // the data, transformed in place

    // in-place transform of buf, kFftSize points; no allocation
    void transform(bool inverse)
    {
        plan.exec(reinterpret_cast<FftCplx*>(buf), inverse ? 1.0 / kFftSize : 1.0, !inverse, scratch);
    }

private:
    FftPlan plan;
    FftCplx scratch[kFftSize];
};

// In-place FFT of any power-of-two length, same convention. It builds a plan and allocates, so it serves the tests and tools, never
// the audio thread.
inline void fft(cplx* x, int log2n, bool inverse)
{
    const std::size_t n = std::size_t(1) << log2n;
    const FftPlan plan(n);
    plan.exec(reinterpret_cast<FftCplx*>(x), inverse ? 1.0 / double(n) : 1.0, !inverse);
}

// ------------------------------------------------------------------------------------------------ pairing
struct RootSet {
    cplx c[kMaxRoots]; int nc = 0;   // complex, one representative per conjugate pair (imag > 0)
    double r[kMaxRoots]; int nr = 0; // real
};

inline void splitRoots(const cplx* roots, int n, RootSet& out)
{
    out.nc = out.nr = 0;
    for (int i = 0; i < n; ++i) {
        const cplx v = roots[i];
        if (std::fabs(v.imag()) <= 1e-9 * std::fmax(1.0, std::abs(v))) out.r[out.nr++] = v.real();
        else if (v.imag() > 0.0) out.c[out.nc++] = v;
    }
}

struct Section { cplx z[2]; int nz = 0; cplx p[2]; int np = 0; };

inline cplx zmap(cplx s, double T) { return std::exp(s * T); }

inline int pairSections(const AnalogZpk& A, double T, Section* out)
{
    // One pass over every pole, the one nearest the unit circle first (complex pairs and real poles together, as scipy's zpk2sos
    // 'nearest'); each takes the zeros nearest to it. Poles near z = 1 (the low frequencies) therefore meet the zeros near z = 1, which
    // keeps every section close to unity away from its own frequency and keeps the cascade well conditioned in double precision.
    RootSet P, Z;
    splitRoots(A.p, A.np, P);
    splitRoots(A.z, A.nz, Z);
    int ns = 0;
    bool usedC[kMaxRoots] = {}, usedR[kMaxRoots] = {};
    int left = P.nc + P.nr;
    while (left > 0) {
        // the pole nearest the unit circle
        int bc = -1, br = -1; double bm = -1.0;
        for (int i = 0; i < P.nc; ++i) if (!usedC[i]) { const double m = std::abs(zmap(P.c[i], T)); if (m > bm) { bm = m; bc = i; br = -1; } }
        for (int i = 0; i < P.nr; ++i) if (!usedR[i]) { const double m = std::exp(P.r[i] * T); if (m > bm) { bm = m; br = i; bc = -1; } }
        Section S;
        if (bc >= 0) {
            usedC[bc] = true; --left;
            S.p[0] = P.c[bc]; S.p[1] = std::conj(P.c[bc]); S.np = 2;
        } else {
            usedR[br] = true; --left;
            S.p[0] = P.r[br]; S.np = 1;
            // its partner: the unused real pole nearest to it in the z-plane
            int bj = -1; double bd = 1e300;
            for (int i = 0; i < P.nr; ++i) if (!usedR[i]) { const double d = std::fabs(std::exp(P.r[i] * T) - std::exp(P.r[br] * T)); if (d < bd) { bd = d; bj = i; } }
            if (bj >= 0) { usedR[bj] = true; --left; S.p[1] = P.r[bj]; S.np = 2; }
        }
        const cplx zp = zmap(S.p[0], T);
        // zeros: the nearest complex pair (two-pole sections only) or the nearest real zeros, whichever is closer to the primary pole
        int ci = -1; double dc = 1e300;
        if (S.np == 2) for (int i = 0; i < Z.nc; ++i) { const double d = std::abs(zmap(Z.c[i], T) - zp); if (d < dc) { dc = d; ci = i; } }
        int ra = -1, rb = -1; double da = 1e300, db = 1e300;
        for (int i = 0; i < Z.nr; ++i) {
            const double d = std::abs(zmap(cplx(Z.r[i], 0.0), T) - zp);
            if (d < da) { db = da; rb = ra; da = d; ra = i; } else if (d < db) { db = d; rb = i; }
        }
        const int haveR = Z.nr < S.np ? Z.nr : S.np;
        const bool takeC = ci >= 0 && (haveR == 0 || dc <= da);
        if (takeC) { S.z[0] = Z.c[ci]; S.z[1] = std::conj(Z.c[ci]); S.nz = 2; Z.c[ci] = Z.c[--Z.nc]; }
        else if (haveR >= 1) {
            S.z[0] = Z.r[ra]; S.nz = 1;
            if (haveR == 2) {
                S.z[1] = Z.r[rb]; S.nz = 2;
                const int hi = ra > rb ? ra : rb, lo = ra > rb ? rb : ra;
                Z.r[hi] = Z.r[--Z.nr]; Z.r[lo] = Z.r[--Z.nr];
            } else { Z.r[ra] = Z.r[--Z.nr]; }
        }
        out[ns++] = S;
    }
    // zeros left over (more zeros than poles in some region, or a complex pair with no two-pole section free): FIR sections,
    // stable by construction
    while (Z.nc > 0) { Section S; S.z[0] = Z.c[Z.nc - 1]; S.z[1] = std::conj(Z.c[Z.nc - 1]); S.nz = 2; --Z.nc; out[ns++] = S; }
    while (Z.nr > 0) {
        Section S; S.z[0] = Z.r[--Z.nr]; S.nz = 1;
        if (Z.nr > 0) { S.z[1] = Z.r[--Z.nr]; S.nz = 2; }
        out[ns++] = S;
    }
    return ns;
}

// ------------------------------------------------------------------------------------------------ design
// |1 - exp((r - j w) T)| computed without cancellation for small arguments
inline double digitalFactorMag(cplx r, double w, double T)
{
    const double x = r.real() * T, y = (r.imag() - w) * T;
    const double em1 = std::expm1(x), ex = em1 + 1.0;
    const double s = std::sin(0.5 * y);
    const double re = em1 * std::cos(y) - 2.0 * s * s;   // e^x cos y - 1
    const double im = ex * std::sin(y);
    return std::hypot(re, im);
}

// work: caller-owned FFT plan and buffers (keeps this free of allocation and of static or thread-local storage)
inline bool designDigital(const ChannelSetting& cs, double fs, DigitalDesign& D, DesignWork& work)
{
    D.nsec = 0; D.gain = 1.0; D.identity = true;
    for (int i = 0; i < kFirTaps; ++i) D.fir[i] = 0.0;
    D.fir[0] = 1.0;
    const AnalogZpk A = channelZpk(cs);
    if (!A.ok) return false;
    if (A.np == 0 && A.nz == 0) { D.gain = A.gain; D.identity = (A.gain == 1.0); return true; }
    D.identity = false;
    const double T = 1.0 / fs;
    Section S[kMaxSections];
    const int ns = pairSections(A, T, S);
    // matched-z sections; each section's gain matches the analog section at a reference frequency
    const double wr = 2.0 * kPi * std::fmin(1000.0, 0.05 * fs);
    double g = A.gain;
    cplx dz[kMaxRoots], dp[kMaxRoots]; int ndz = 0, ndp = 0;
    for (int k = 0; k < ns; ++k) {
        const Section& s = S[k];
        Biquad q { 1.0, 0.0, 0.0, 0.0, 0.0 };
        cplx zd[2], pd[2];
        for (int i = 0; i < s.nz; ++i) { zd[i] = zmap(s.z[i], T); dz[ndz++] = zd[i]; }
        for (int i = 0; i < s.np; ++i) { pd[i] = zmap(s.p[i], T); dp[ndp++] = pd[i]; }
        if (s.nz == 2) { q.b1 = -(zd[0] + zd[1]).real(); q.b2 = (zd[0] * zd[1]).real(); }
        else if (s.nz == 1) { q.b1 = -zd[0].real(); }
        if (s.np == 2) { q.a1 = -(pd[0] + pd[1]).real(); q.a2 = (pd[0] * pd[1]).real(); }
        else if (s.np == 1) { q.a1 = -pd[0].real(); }
        // gain match at wr
        const cplx e1 = std::exp(cplx(0.0, -wr * T)), e2 = e1 * e1, jw(0.0, wr);
        const cplx hd = (q.b0 + q.b1 * e1 + q.b2 * e2) / (1.0 + q.a1 * e1 + q.a2 * e2);
        cplx ha = 1.0;
        for (int i = 0; i < s.nz; ++i) ha *= (jw - s.z[i]);
        for (int i = 0; i < s.np; ++i) ha /= (jw - s.p[i]);
        const double k1 = std::abs(ha) / std::abs(hd);
        g *= k1;   // the section gain is folded into the one scalar gain
        D.sec[D.nsec++] = q;
    }
    D.gain = g;
    // minimum-phase FIR correction of the magnitude, analog over matched, factor by factor (no under/overflow)
    cplx* buf = work.buf;
    const int half = kFftSize / 2;
    const double dw = 2.0 * kPi * fs / kFftSize;
    // log|H_matched| = log|g| + sum_z log|1 - e^{qT} e^{-jwT}| - sum_p (...), with the section gains already in g.
    // The per-section gain match makes log|g| = log|A.gain| + sum over sections of log(k1); compute the ratio directly per root:
    // log E(w) = log|A.gain| - log|g| + sum_z [log|jw - q| - log|1 - e^{(q - jw)T}|] - sum_p [same]
    const double lg = std::log(std::fabs(A.gain)) - std::log(std::fabs(g));
    for (int k = 0; k <= half; ++k) {
        const double w = k == 0 ? 1e-3 * dw : k * dw;
        double L = lg;
        for (int i = 0; i < A.nz; ++i) L += std::log(std::abs(cplx(0.0, w) - A.z[i])) - std::log(digitalFactorMag(A.z[i], w, T));
        for (int i = 0; i < A.np; ++i) L -= std::log(std::abs(cplx(0.0, w) - A.p[i])) - std::log(digitalFactorMag(A.p[i], w, T));
        buf[k] = L;
    }
    for (int k = half + 1; k < kFftSize; ++k) buf[k] = buf[kFftSize - k];
    work.transform(true);                          // real cepstrum
    buf[0] = buf[0].real();
    for (int k = 1; k < half; ++k) buf[k] = 2.0 * buf[k].real();
    buf[half] = buf[half].real();
    for (int k = half + 1; k < kFftSize; ++k) buf[k] = 0.0;
    work.transform(false);                         // minimum-phase log spectrum
    for (int k = 0; k < kFftSize; ++k) buf[k] = std::exp(buf[k]);
    work.transform(true);                          // minimum-phase impulse response
    for (int i = 0; i < kFirTaps; ++i) D.fir[i] = buf[i].real();
    for (int i = 0; i < D.nsec; ++i) {
        const Biquad& q = D.sec[i];
        if (!std::isfinite(q.b0 + q.b1 + q.b2 + q.a1 + q.a2)) return false;
    }
    for (int i = 0; i < kFirTaps; ++i) if (!std::isfinite(D.fir[i])) return false;
    return std::isfinite(D.gain);
}

// digital response of a design (tests)
inline cplx digitalResponse(const DigitalDesign& D, double fHz, double fs)
{
    const cplx e1 = std::exp(cplx(0.0, -2.0 * kPi * fHz / fs)), e2 = e1 * e1;
    cplx H = D.gain;
    for (int i = 0; i < D.nsec; ++i) {
        const Biquad& q = D.sec[i];
        H *= (q.b0 + q.b1 * e1 + q.b2 * e2) / (1.0 + q.a1 * e1 + q.a2 * e2);
    }
    cplx F = 0.0, e = 1.0;
    for (int i = 0; i < kFirTaps; ++i) { F += D.fir[i] * e; e *= e1; }
    return H * F;
}

} // namespace cpeq
