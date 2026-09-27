// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// Unit tests for the DSP core (no plugin host needed). scripts/build.sh builds and runs them before the plugin; by hand, from the
// repository root:
//   g++ -std=gnu++17 -O2 -Wall -Wextra -Isrc -o build/tests/test_dsp tests/test_dsp.cpp && build/tests/test_dsp build/tests
// Checks, each printed with its measured figure and PASS/FAIL:
//  1. eigenvalue route: the zeros/poles/gain response equals direct evaluation of the network (so the eigen solver and the
//     state-space realisation are right), random settings;
//  2. discretisation accuracy: digital magnitude against the analog network, 10 Hz to min(20 kHz, 0.45 fs), six sample rates;
//  3. stability and minimum phase: every digital pole strictly inside the unit circle, every section zero on or inside it, FIR
//     correction minimum phase (its energy is front-loaded and its cepstrum is causal by construction);
//  4. runtime: the impulse response of ChannelEngine, transformed, equals the design's response (magnitude and phase) on a grid;
//  5. click-free changes: stepping settings while noise plays gives finite, bounded output and settles to the new filter;
//  6. identity: flat settings and bypass are bit-transparent.
// Also writes <out_dir>/cpp_reference.json (settings, analog and digital responses) for the Python cross-check.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include <string>
#include "dsp/Engine.hpp"

using namespace cpeq;

static int gFail = 0;
static void report(const char* name, bool ok, const char* detail)
{
    std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) ++gFail;
}

static ChannelSetting randomSetting(std::mt19937& rng, bool filters)
{
    std::uniform_int_distribution<int> m(0, 2), t(0, 1), g(0, 15), f(0, 10), tr(0, 10), fl(0, 5);
    ChannelSetting cs;
    for (int b = 0; b < kBands; ++b) { cs.band[b].mode = m(rng); cs.band[b].type = t(rng); cs.band[b].gainStep = g(rng); cs.band[b].bwStep = g(rng); cs.band[b].freqIdx = f(rng); }
    cs.eqIn = true; cs.trimIdx = tr(rng);
    cs.lowPassIdx = filters ? fl(rng) : 0; cs.highPassIdx = filters ? fl(rng) : 0;
    return cs;
}

static double db(cplx h) { return 20.0 * std::log10(std::abs(h)); }

static double pct(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = p / 100.0 * double(v.size() - 1);
    const size_t i = size_t(idx); const double fr = idx - double(i);
    return i + 1 < v.size() ? v[i] * (1 - fr) + v[i + 1] * fr : v[i];
}

static std::string settingJson(const ChannelSetting& cs)
{
    std::string s = "{\"bands\":[";
    char b[160];
    for (int i = 0; i < kBands; ++i) {
        std::snprintf(b, sizeof b, "%s{\"mode\":%d,\"type\":%d,\"gain_step\":%d,\"bw_step\":%d,\"freq_idx\":%d}", i ? "," : "",
                      cs.band[i].mode, cs.band[i].type, cs.band[i].gainStep, cs.band[i].bwStep, cs.band[i].freqIdx);
        s += b;
    }
    std::snprintf(b, sizeof b, "],\"eq_in\":%s,\"trim_idx\":%d,\"lp_idx\":%d,\"hp_idx\":%d}", cs.eqIn ? "true" : "false", cs.trimIdx, cs.lowPassIdx, cs.highPassIdx);
    s += b;
    return s;
}

int main(int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    std::mt19937 rng(20260926);
    char buf[512];
    static DesignWork work;

    // 1. eigen route against direct evaluation
    {
        double worstDb = 0, worstPh = 0; int nfail = 0;
        for (int it = 0; it < 3000; ++it) {
            const ChannelSetting cs = randomSetting(rng, it % 2 == 0);
            const AnalogZpk A = channelZpk(cs);
            if (!A.ok) { ++nfail; continue; }
            for (int k = 0; k < 60; ++k) {
                const double f = 10.0 * std::pow(2400.0, k / 59.0);
                const cplx s(0.0, 2 * kPi * f);
                cplx h = A.gain;
                for (int i = 0; i < A.nz; ++i) h *= (s - A.z[i]);
                for (int i = 0; i < A.np; ++i) h /= (s - A.p[i]);
                const cplx d = channelResponseDirect(cs, f);
                worstDb = std::fmax(worstDb, std::fabs(db(h) - db(d)));
                worstPh = std::fmax(worstPh, std::fabs(std::arg(h / d)));
            }
        }
        std::snprintf(buf, sizeof buf, "3000 random settings: max |dB| difference %.3g, max phase difference %.3g rad, eigen failures %d", worstDb, worstPh, nfail);
        report("eigenvalue route equals direct network evaluation", worstDb < 1e-7 && worstPh < 1e-7 && nfail == 0, buf);
    }

    // 2 and 3. discretisation accuracy, stability, minimum phase
    const double rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
    for (double fs : rates) {
        std::vector<double> e1, e2, e3; int unstable = 0, zeroOut = 0, firLate = 0, designFail = 0; double maxZero = 0;
        const double top = std::fmin(20000.0, 0.45 * fs);
        for (int it = 0; it < 1500; ++it) {
            const ChannelSetting cs = randomSetting(rng, it % 3 == 0);
            DigitalDesign D;
            if (!designDigital(cs, fs, D, work)) { ++designFail; continue; }
            double m1 = 0, m2 = 0, m3 = 0;
            for (int k = 0; k < 400; ++k) {
                const double f = 10.0 * std::pow(top / 10.0, k / 399.0);
                const double e = std::fabs(db(digitalResponse(D, f, fs)) - db(channelResponseDirect(cs, f)));
                if (f <= 5000) m1 = std::fmax(m1, e); else if (f <= 12000) m2 = std::fmax(m2, e); else m3 = std::fmax(m3, e);
            }
            e1.push_back(m1); e2.push_back(m2); if (top > 12000) e3.push_back(m3);
            for (int i = 0; i < D.nsec; ++i) {
                const Biquad& q = D.sec[i];
                // poles: z^2 + a1 z + a2; |z| < 1 iff |a2| < 1 and |a1| < 1 + a2
                if (!(std::fabs(q.a2) < 1.0 && std::fabs(q.a1) < 1.0 + q.a2)) ++unstable;
                // zeros: b0 z^2 + b1 z + b2 on or inside the unit circle (the matched-z image of the left half plane)
                // stable quadratic roots: qq = -(b1 + sign(b1) sqrt(disc)) / 2, z1 = qq / b0, z2 = b2 / qq
                const cplx disc = std::sqrt(cplx(q.b1 * q.b1 - 4 * q.b0 * q.b2, 0.0));
                const cplx qq = -0.5 * (q.b1 + (q.b1 >= 0 ? disc : -disc));
                const double r1 = std::abs(qq / q.b0), r2 = (q.b2 != 0.0 && std::abs(qq) > 0) ? std::abs(q.b2 / qq) : 0.0;
                maxZero = std::fmax(maxZero, std::fmax(r1, r2));
                if (r1 > 1.0 + 1e-9 || r2 > 1.0 + 1e-9) ++zeroOut;
            }
            double eHead = 0, eAll = 0;
            for (int i = 0; i < kFirTaps; ++i) { eAll += D.fir[i] * D.fir[i]; if (i < 8) eHead += D.fir[i] * D.fir[i]; }
            if (eHead < 0.99 * eAll) ++firLate;
        }
        std::snprintf(buf, sizeof buf, "fs %.0f: |dB error| vs analog, <=5 kHz p95 %.4f max %.4f; 5-12 kHz p95 %.4f max %.4f; 12 kHz-%.0f p95 %.4f max %.4f",
                      fs, pct(e1, 95), pct(e1, 100), pct(e2, 95), pct(e2, 100), top, pct(e3, 95), pct(e3, 100));
        report("magnitude follows the analog network", pct(e1, 100) < 0.02 && pct(e2, 100) < 0.05 && pct(e3, 100) < 0.1, buf);
        std::snprintf(buf, sizeof buf, "fs %.0f: sections with a pole on/outside the circle %d, with a zero outside %d (largest zero radius %.12f; the high-pass zeros sit on z = 1), FIR with <99%% energy in 8 taps %d, design failures %d",
                      fs, unstable, zeroOut, maxZero, firLate, designFail);
        report("stable and minimum phase", unstable == 0 && zeroOut == 0 && firLate == 0 && designFail == 0, buf);
    }

    // 4. runtime impulse response against the design: (a) the double-precision cascade itself, (b) the engine with float I/O
    {
        double worstDb = 0, worstPh = 0, worstDbF = 0; double wfs = 0, wf = 0;
        static ChannelEngine eng;
        static Cascade cas;
        const int N = 1 << 18;   // 1.36 s at 192 kHz: the slowest decays (about 50 ms at 22 Hz) are far below the float floor by the end
        std::vector<float> x(N, 0.0f), y(N);
        std::vector<cplx> Y(N), YF(N);
        for (int it = 0; it < 360; ++it) {
            const double fs = rates[it % 6];
            const ChannelSetting cs = randomSetting(rng, it % 2 == 0);
            DigitalDesign D; designDigital(cs, fs, D, work);
            cas.d = D; cas.reset();
            for (int i = 0; i < N; ++i) Y[size_t(i)] = cas.tick(i == 0 ? 1.0 : 0.0);
            fft(Y.data(), 18, false);
            eng.prepare(fs); eng.setTarget(cs);
            std::fill(x.begin(), x.end(), 0.0f); x[0] = 1.0f;
            eng.process(x.data(), y.data(), N);
            for (int i = 0; i < N; ++i) YF[size_t(i)] = double(y[size_t(i)]);
            fft(YF.data(), 18, false);
            for (int k = 1; k < N / 2; k += 53) {
                const double f = k * fs / N;
                if (f < 10 || f > std::fmin(20000.0, 0.45 * fs)) continue;
                const cplx h = digitalResponse(D, f, fs);
                if (std::abs(h) < 1e-3) continue;   // compare where the response is above -60 dB
                const double e = std::fabs(db(Y[size_t(k)]) - db(h));
                if (e > worstDb) { worstDb = e; wfs = fs; wf = f; }
                worstPh = std::fmax(worstPh, std::fabs(std::arg(Y[size_t(k)] / h)));
                worstDbF = std::fmax(worstDbF, std::fabs(db(YF[size_t(k)]) - db(h)));
            }
        }
        std::snprintf(buf, sizeof buf, "360 settings over six rates, bins above -60 dB: double cascade vs design max |dB| %.2e (at %.0f Hz, fs %.0f), max phase %.2e rad; "
                      "engine with float I/O max |dB| %.2e", worstDb, wf, wfs, worstPh, worstDbF);
        report("runtime cascade equals the design", worstDb < 1e-4 && worstPh < 1e-4 && worstDbF < 2e-2, buf);
    }

    // 5. click-free changes
    {
        static ChannelEngine eng, ref;
        const double fs = 48000; const int N = 48000 * 3;
        std::vector<float> x(N), y(N), yr(N);
        std::normal_distribution<float> nd(0.0f, 0.1f);
        for (auto& v : x) v = nd(rng);
        eng.prepare(fs);
        ChannelSetting a = randomSetting(rng, true), b = randomSetting(rng, true);
        eng.setTarget(a);
        double maxAbs = 0; bool finite = true;
        // change the setting every 7 ms for one second (faster than a fade), then hold b
        int pos = 0; const int blk = 336;
        for (int k = 0; pos < N; ++k) {
            const int n = std::min(blk, N - pos);
            if (pos < 48000) eng.setTarget(k % 2 ? a : b); else eng.setTarget(b);
            eng.process(&x[size_t(pos)], &y[size_t(pos)], n);
            pos += n;
        }
        for (int i = 0; i < N; ++i) { finite &= std::isfinite(y[size_t(i)]); maxAbs = std::fmax(maxAbs, std::fabs(y[size_t(i)])); }
        // bound: the louder of the two settings run steadily on the same noise
        double peakA = 0, peakB = 0;
        ref.prepare(fs); ref.setTarget(a); ref.process(x.data(), yr.data(), N); for (float v : yr) peakA = std::fmax(peakA, std::fabs(v));
        // the last second must equal a fresh engine that ran b all along (after its own start-up)
        ref.prepare(fs); ref.setTarget(b); ref.process(x.data(), yr.data(), N); for (float v : yr) peakB = std::fmax(peakB, std::fabs(v));
        double err = 0, sig = 0;
        for (int i = 2 * 48000; i < N; ++i) { err += std::pow(double(y[size_t(i)]) - yr[size_t(i)], 2); sig += std::pow(double(yr[size_t(i)]), 2); }
        const double rel = 10 * std::log10(err / sig + 1e-300);
        const double bound = std::fmax(peakA, peakB);
        std::snprintf(buf, sizeof buf, "setting toggled every 7 ms for 1 s: finite %s, peak %.3f against %.3f for the louder setting held; settled output vs a fresh engine %.1f dB",
                      finite ? "yes" : "no", maxAbs, bound, rel);
        report("changes are click-free and settle", finite && maxAbs < 1.25 * bound && rel < -120.0, buf);
    }

    // 6. identity
    {
        static ChannelEngine eng;
        const int N = 4096; std::vector<float> x(N), y(N);
        std::normal_distribution<float> nd(0.0f, 0.3f);
        for (auto& v : x) v = nd(rng);
        ChannelSetting flat;   // all bands OUT, trim 0, filters off
        eng.prepare(48000); eng.setTarget(flat); eng.process(x.data(), y.data(), N);
        const bool same1 = std::memcmp(x.data(), y.data(), sizeof(float) * N) == 0;
        ChannelSetting byp = randomSetting(rng, true); byp.eqIn = false;
        eng.prepare(48000); eng.setTarget(byp); eng.process(x.data(), y.data(), N);
        const bool same2 = std::memcmp(x.data(), y.data(), sizeof(float) * N) == 0;
        ChannelSetting g0 = randomSetting(rng, false);   // every band at gain step 0 is flat (the gain pot at its flat end)
        for (int b = 0; b < kBands; ++b) g0.band[b].gainStep = 0;
        g0.trimIdx = 5;
        eng.prepare(48000); eng.setTarget(g0); eng.process(x.data(), y.data(), N);
        const bool same3 = std::memcmp(x.data(), y.data(), sizeof(float) * N) == 0;
        std::snprintf(buf, sizeof buf, "all OUT: %s; IN off: %s; every gain at step 0: %s", same1 ? "bit-identical" : "differs",
                      same2 ? "bit-identical" : "differs", same3 ? "bit-identical" : "differs");
        report("flat settings and bypass are transparent", same1 && same2 && same3, buf);
    }

    // reference JSON for the Python cross-check (tests/crosscheck.py)
    {
        const std::string path = outDir + "/cpp_reference.json";
        FILE* fp = std::fopen(path.c_str(), "w");
        std::fprintf(fp, "{\"freqs\":[");
        std::vector<double> fr;
        for (int k = 0; k < 120; ++k) fr.push_back(10.0 * std::pow(2000.0, k / 119.0));
        for (size_t k = 0; k < fr.size(); ++k) std::fprintf(fp, "%s%.10g", k ? "," : "", fr[k]);
        std::fprintf(fp, "],\"cases\":[");
        std::mt19937 r2(7);
        for (int it = 0; it < 300; ++it) {
            ChannelSetting cs = randomSetting(r2, it % 2 == 0);
            const double fs = rates[it % 6];
            DigitalDesign D; designDigital(cs, fs, D, work);
            std::fprintf(fp, "%s{\"setting\":%s,\"fs\":%.1f,\"analog\":[", it ? "," : "", settingJson(cs).c_str(), fs);
            for (size_t k = 0; k < fr.size(); ++k) { const cplx h = channelResponseDirect(cs, fr[k]); std::fprintf(fp, "%s[%.17g,%.17g]", k ? "," : "", h.real(), h.imag()); }
            std::fprintf(fp, "],\"digital\":[");
            for (size_t k = 0; k < fr.size(); ++k) {
                const cplx h = fr[k] < 0.5 * fs ? digitalResponse(D, fr[k], fs) : cplx(0.0, 0.0);
                std::fprintf(fp, "%s[%.17g,%.17g]", k ? "," : "", h.real(), h.imag());
            }
            std::fprintf(fp, "]}");
        }
        std::fprintf(fp, "]}\n");
        std::fclose(fp);
        std::printf("wrote %s\n", path.c_str());
    }

    std::printf("%s: %d failing check(s)\n", gFail ? "FAILED" : "ALL PASSED", gFail);
    return gFail ? 1 : 0;
}
