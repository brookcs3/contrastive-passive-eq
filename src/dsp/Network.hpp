// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// The passive network of one channel, from the panel's stepped positions to analog poles, zeros and gain.
// This is the C++ form of tests/ref_model.py (the reference the tests compare against); keep the two in step.
//
// Normalised impedance units: the boost divider's flat series conductance is 1 and its flat shunt conductance is p, so
//   boost divider  B(Y) = (1 + p)(1 + Y) / (1 + p + Y),   Y = sum of the boost-side branch admittances (parallel bands, one divider)
//   cut divider    C(Y) = 1 / (1 + Y),                     Y = sum of the cut-side branch admittances (shunt leg)
// and one channel is H(s) = B * HP * LP * C * trim (the modelled unit's signal order: boost, filters, tube gain, cut, gain trim).
// A branch admittance is one of
//   bp2  Y = G s / (s^2 + a1 s + a0)   series R-L-C (bells, dips and bumps, the 16K/27K shelf with its 50 kHz series L, 22/33 Hz shelves)
//   hp1  Y = G s / (s + a1)            series R-C (high shelf)
//   lp1  Y = G / (s + a1)              series R-L (low shelf, the 22/33 Hz bandwidth low cut)
// Zeros and poles come from state-space eigenvalues (EigenReal.hpp): for Y = d + c (sI - A)^-1 b, the zeros of k + Y are eig(A - b c / (k + d)).
#pragma once
#include <cmath>
#include <complex>
#include "EigenReal.hpp"
#include "FittedConstants.hpp"

namespace cpeq {

using cplx = std::complex<double>;
static constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------------ panel positions
static constexpr int kBands = 4;
static constexpr int kFreqPositions = 11;
static constexpr int kGainSteps = 16;       // detents 0..15, 0 = CCW (flat), 15 = CW (top of the stepped range)
static constexpr int kBandwidthSteps = 16;  // detents 0..15, 0 = CCW (wide), 15 = CW (narrow)
static constexpr int kTrimPositions = 11;   // -2.5 .. +2.5 dB in 0.5 dB steps
static constexpr int kLowPassPositions = 6; // OFF 52K 40K 27K 20K 15K
static constexpr int kHighPassPositions = 6;// OFF 12 16 23 30 39

static constexpr double kBandFreqHz[kBands][kFreqPositions] = {
    { 22, 33, 47, 68, 100, 150, 220, 330, 470, 680, 1000 },
    { 82, 120, 180, 270, 390, 560, 820, 1200, 1800, 2700, 3900 },
    { 220, 330, 470, 680, 1000, 1500, 2200, 3300, 4700, 6800, 10000 },
    { 560, 820, 1200, 1800, 2700, 3900, 5600, 8200, 12000, 16000, 27000 },
};
static constexpr double kLowPassHz[kLowPassPositions] = { 0, 52000, 40000, 27000, 20000, 15000 };
static constexpr double kHighPassHz[kHighPassPositions] = { 0, 12, 16, 23, 30, 39 };

enum BandMode { kBoost = 0, kOut = 1, kCut = 2 };
enum BandType { kShelf = 0, kBell = 1 };

struct BandSetting {
    int mode = kOut, type = kBell, gainStep = 0, bwStep = 7, freqIdx = 5;
    bool operator==(const BandSetting& o) const {
        return mode == o.mode && type == o.type && gainStep == o.gainStep && bwStep == o.bwStep && freqIdx == o.freqIdx;
    }
};

struct ChannelSetting {
    BandSetting band[kBands];
    bool eqIn = true;
    int trimIdx = 5, lowPassIdx = 0, highPassIdx = 0;
    bool operator==(const ChannelSetting& o) const {
        for (int i = 0; i < kBands; ++i) if (!(band[i] == o.band[i])) return false;
        return eqIn == o.eqIn && trimIdx == o.trimIdx && lowPassIdx == o.lowPassIdx && highPassIdx == o.highPassIdx;
    }
    bool operator!=(const ChannelSetting& o) const { return !(*this == o); }
};

inline double trimDb(int idx) { return -2.5 + 0.5 * idx; }

// ------------------------------------------------------------------------------------------------ pots
// gain pot: reverse-log taper through the fitted 12:00 position; the stepped panel tops out at a_max (11 dB on the narrowest bell).
inline double gainDivider(int step)
{
    if (step <= 0) return 0.0;
    const double th = step / double(kGainSteps - 1), b = K::gain_taper_b;
    return K::a_max_mastering * (1.0 - std::pow(b, -th)) / (1.0 - 1.0 / b);
}
// bandwidth pot: log-resistance quadratic through the three fitted anchors [CCW, 12:00, CW] at theta 0, 0.5, 1
inline double taper3(const double* anch, double th)
{
    const double y0 = std::log(anch[0]), y1 = std::log(anch[1]), y2 = std::log(anch[2]);
    const double c = 2.0 * (y2 - 2.0 * y1 + y0), b = y2 - y0 - c;
    return std::exp(y0 + b * th + c * th * th);
}
inline double lin3(const double* anch, double th)
{
    return th <= 0.5 ? anch[0] + (anch[1] - anch[0]) * th / 0.5 : anch[1] + (anch[2] - anch[1]) * (th - 0.5) / 0.5;
}

// ------------------------------------------------------------------------------------------------ branches
enum BranchKind { kBP2 = 0, kHP1 = 1, kLP1 = 2 };
struct Branch { int kind; double G, a1, a0; };
static constexpr int kMaxBranches = 2 * kBands;

struct BranchList {
    Branch b[kMaxBranches];
    int n = 0;
    void add(int kind, double G, double a1, double a0 = 0.0) { if (n < kMaxBranches) b[n++] = Branch { kind, G, a1, a0 }; }
};

inline void buildBranches(const ChannelSetting& cs, BranchList& boost, BranchList& cut)
{
    boost.n = cut.n = 0;
    for (int k = 0; k < kBands; ++k) {
        const BandSetting& bd = cs.band[k];
        if (bd.mode == kOut) continue;
        const double g = gainDivider(bd.gainStep);
        if (g <= 0.0) continue;
        const double th = bd.bwStep / double(kBandwidthSteps - 1);
        const double fz = kBandFreqHz[k][bd.freqIdx], w0 = 2.0 * kPi * fz;
        const double src = g * (1.0 - g) * K::Rp;
        const bool isBoost = bd.mode == kBoost;
        BranchList& own = isBoost ? boost : cut;
        BranchList& opp = isBoost ? cut : boost;
        if (bd.type == kBell) {
            const double R = (isBoost ? taper3(K::bell_boost_R, th) : taper3(K::bell_cut_R, th)) + src;
            const double m = isBoost ? 1.0 : K::m_cut, X = K::X0;
            own.add(kBP2, m * g * w0 / X, R * w0 / X, w0 * w0);
            continue;
        }
        // SHELF: bands 1-2 low shelves, bands 3-4 high shelves
        const double mOwn = isBoost ? 1.0 : K::m_cut_shelf;
        const bool high = k >= 2;
        const bool special = (k == 0) && (fz == 22.0 || fz == 33.0);
        const bool air = (k == 3) && (fz == 16000.0 || fz == 27000.0);
        double dipR = 0.0;
        if (high) {
            const double Rs = K::hs_Rs + src, Xs = K::hs_Xs;
            if (air) {
                const double w50 = 2.0 * kPi * K::air_lpf_hz, Lr = K::hs_Rs / w50;   // series L whose reactance is Rs at 50 kHz
                own.add(kBP2, mOwn * g / Lr, Rs / Lr, Xs * w0 / Lr);
            } else {
                own.add(kHP1, mOwn * g / Rs, Xs * w0 / Rs);
            }
            dipR = taper3(K::hs_dip_R, th);
        } else if (special) {
            const double Rs0 = fz == 22.0 ? K::sp22_Rs : K::sp33_Rs, Xs = fz == 22.0 ? K::sp22_Xs : K::sp33_Xs;
            const double Kc = lin3(K::sp_K, th), Rs = Rs0 + lin3(K::sp_Rx, th) + src;
            if (Kc > 1e-9) own.add(kBP2, mOwn * g * w0 / Xs, Rs * w0 / Xs, Kc * w0 * w0 / Xs);
            else           own.add(kLP1, mOwn * g * w0 / Xs, Rs * w0 / Xs);
            const double Gh = lin3(K::sp_Gh, th);
            if (Gh > 1e-9) {   // the bandwidth low cut: series R-L in the opposite section
                const double Rh = 1.0 / Gh + src, mh = isBoost ? K::m_cut : 1.0;
                opp.add(kLP1, mh * g * w0 / K::sp_Xh, Rh * w0 / K::sp_Xh);
            }
            continue;
        } else {
            const double Rs = K::ls_Rs + src, Xs = K::ls_Xs;
            own.add(kLP1, mOwn * g * w0 / Xs, Rs * w0 / Xs);
            dipR = taper3(K::ls_dip_R, th);
        }
        // the band's own bell in the opposite section: a dip while shelf boosting, a bump while shelf cutting
        const double wd = air ? 2.0 * kPi * K::air_dip_hz : w0, X = K::X0;
        const double R = isBoost ? dipR + src : dipR + K::bump_dR + src;
        const double m = isBoost ? K::m_cut : 1.0;
        opp.add(kBP2, m * g * wd / X, R * wd / X, wd * wd);
    }
}

inline cplx branchSum(const BranchList& L, cplx s)
{
    cplx Y = 0.0;
    for (int i = 0; i < L.n; ++i) {
        const Branch& b = L.b[i];
        if (b.kind == kBP2) Y += b.G * s / (s * s + b.a1 * s + b.a0);
        else if (b.kind == kHP1) Y += b.G * s / (s + b.a1);
        else Y += b.G / (s + b.a1);
    }
    return Y;
}

// ------------------------------------------------------------------------------------------------ filters
// Chebyshev type I, ripple fitted to the flattest printed high passes (0.14 dB), -3 dB at the switch frequency.
struct FilterZpk { cplx p[5]; int np = 0; int nzOrigin = 0; double gain = 1.0; };

inline void chebyshevPrototype(int order, cplx* poles)   // low pass, -3 dB at 1 rad/s, unity DC gain for odd order
{
    const double eps = std::sqrt(std::pow(10.0, K::filter_ripple_db / 10.0) - 1.0);
    const double v = std::asinh(1.0 / eps) / order;
    const double w3 = std::cosh(std::acosh(1.0 / eps) / order);   // |T_n(w3)| = 1/eps: the -3 dB point of the ripple-normalised prototype
    for (int k = 1; k <= order; ++k) {
        const double th = kPi * (2 * k - 1) / (2.0 * order);
        poles[k - 1] = cplx(-std::sinh(v) * std::sin(th), std::cosh(v) * std::cos(th)) / w3;
    }
}

inline FilterZpk lowPass(double fc, int order)
{
    FilterZpk f; cplx pr[5]; chebyshevPrototype(order, pr);
    const double wc = 2.0 * kPi * fc; cplx g = 1.0;
    for (int i = 0; i < order; ++i) { f.p[i] = pr[i] * wc; g *= -f.p[i]; }
    f.np = order; f.gain = g.real();
    return f;
}

inline FilterZpk highPass(double fc, int order)
{
    FilterZpk f; cplx pr[5]; chebyshevPrototype(order, pr);
    const double wc = 2.0 * kPi * fc;
    for (int i = 0; i < order; ++i) f.p[i] = wc / pr[i];   // s -> wc / s
    f.np = order; f.nzOrigin = order; f.gain = 1.0;
    return f;
}

// ------------------------------------------------------------------------------------------------ analog zpk of the channel
static constexpr int kMaxRoots = 2 * kMaxBranches * 2 + 3 + 5 + 2;

struct AnalogZpk {
    cplx z[kMaxRoots]; int nz = 0;
    cplx p[kMaxRoots]; int np = 0;
    double gain = 1.0;
    bool ok = true;
    void addZ(cplx v) { if (nz < kMaxRoots) z[nz++] = v; else ok = false; }
    void addP(cplx v) { if (np < kMaxRoots) p[np++] = v; else ok = false; }
};

static constexpr double kWRef = 2.0 * kPi * 1000.0;   // time scaling for conditioning: eigenvalues are found in units of kWRef

struct StateSpace { RealMatrix A; double b[kMaxN], c[kMaxN]; double d = 0.0; };

inline void realise(const BranchList& L, StateSpace& S)
{
    int n = 0;
    for (int i = 0; i < L.n; ++i) n += L.b[i].kind == kBP2 ? 2 : 1;
    S.A.n = n; S.d = 0.0;
    for (int i = 0; i < n; ++i) { S.b[i] = S.c[i] = 0.0; for (int j = 0; j < n; ++j) S.A.a[i][j] = 0.0; }
    int i = 0;
    for (int k = 0; k < L.n; ++k) {
        const Branch& br = L.b[k];
        if (br.kind == kBP2) {
            const double w = std::sqrt(br.a0) / kWRef;
            S.A.a[i][i + 1] = w; S.A.a[i + 1][i] = -w; S.A.a[i + 1][i + 1] = -br.a1 / kWRef;
            S.b[i + 1] = 1.0; S.c[i + 1] = br.G / kWRef; i += 2;
        } else if (br.kind == kHP1) {
            S.A.a[i][i] = -br.a1 / kWRef; S.b[i] = 1.0; S.c[i] = -br.G * br.a1 / kWRef; S.d += br.G; i += 1;
        } else {
            S.A.a[i][i] = -br.a1 / kWRef; S.b[i] = 1.0; S.c[i] = br.G / kWRef; i += 1;
        }
    }
}

// eigenvalues of A - b c / k, scaled back to rad/s
inline bool rankOneEig(const StateSpace& S, double k, cplx* out)
{
    RealMatrix M = S.A;
    for (int i = 0; i < M.n; ++i)
        for (int j = 0; j < M.n; ++j) M.a[i][j] -= S.b[i] * S.c[j] / k;
    const bool ok = eigenvalues(M, out);
    for (int i = 0; i < M.n; ++i) out[i] *= kWRef;
    return ok;
}

inline AnalogZpk channelZpk(const ChannelSetting& cs)
{
    AnalogZpk Z;
    if (!cs.eqIn) return Z;
    BranchList Bb, Cb;
    buildBranches(cs, Bb, Cb);
    const double p = K::p;
    cplx tmp[kMaxN];
    if (Bb.n > 0) {
        StateSpace S; realise(Bb, S);
        Z.ok &= rankOneEig(S, 1.0 + S.d, tmp);        for (int i = 0; i < S.A.n; ++i) Z.addZ(tmp[i]);
        Z.ok &= rankOneEig(S, 1.0 + p + S.d, tmp);    for (int i = 0; i < S.A.n; ++i) Z.addP(tmp[i]);
        Z.gain *= (1.0 + p) * (1.0 + S.d) / (1.0 + p + S.d);
    }
    if (Cb.n > 0) {
        StateSpace S; realise(Cb, S);
        RealMatrix A = S.A; Z.ok &= eigenvalues(A, tmp); for (int i = 0; i < S.A.n; ++i) Z.addZ(tmp[i] * kWRef);
        Z.ok &= rankOneEig(S, 1.0 + S.d, tmp);        for (int i = 0; i < S.A.n; ++i) Z.addP(tmp[i]);
        Z.gain *= 1.0 / (1.0 + S.d);
    }
    if (cs.highPassIdx > 0) {
        const FilterZpk f = highPass(kHighPassHz[cs.highPassIdx], 3);
        for (int i = 0; i < f.np; ++i) Z.addP(f.p[i]);
        for (int i = 0; i < f.nzOrigin; ++i) Z.addZ(0.0);
        Z.gain *= f.gain;
    }
    if (cs.lowPassIdx > 0) {
        const FilterZpk f = lowPass(kLowPassHz[cs.lowPassIdx], cs.lowPassIdx == 1 ? 5 : 3);
        for (int i = 0; i < f.np; ++i) Z.addP(f.p[i]);
        Z.gain *= f.gain;
    }
    Z.gain *= std::pow(10.0, trimDb(cs.trimIdx) / 20.0);
    // a divider pole or zero sits in the open left half plane by passivity; guard against round-off at the axis
    for (int i = 0; i < Z.np; ++i) if (Z.p[i].real() >= 0.0) Z.ok = false;
    return Z;
}

// direct evaluation of the network (the tests' reference; not used on the audio path)
inline cplx channelResponseDirect(const ChannelSetting& cs, double fHz)
{
    if (!cs.eqIn) return 1.0;
    const cplx s(0.0, 2.0 * kPi * fHz);
    BranchList Bb, Cb; buildBranches(cs, Bb, Cb);
    const double p = K::p;
    const cplx YB = branchSum(Bb, s), YC = branchSum(Cb, s);
    cplx H = (1.0 + p) * (1.0 + YB) / (1.0 + p + YB) / (1.0 + YC);
    if (cs.highPassIdx > 0) {
        const FilterZpk f = highPass(kHighPassHz[cs.highPassIdx], 3);
        for (int i = 0; i < f.np; ++i) H *= s / (s - f.p[i]);
    }
    if (cs.lowPassIdx > 0) {
        const FilterZpk f = lowPass(kLowPassHz[cs.lowPassIdx], cs.lowPassIdx == 1 ? 5 : 3);
        cplx h = f.gain; for (int i = 0; i < f.np; ++i) h /= (s - f.p[i]);
        H *= h;
    }
    return H * std::pow(10.0, trimDb(cs.trimIdx) / 20.0);
}

} // namespace cpeq
