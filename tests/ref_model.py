#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Reference model of one channel of the plugin, written as the specification of the C++ DSP (src/dsp/*.hpp). The tests compare the
plugin against it.

params (the plugin's own stepped positions, one channel):
  bands: list of 4 dicts {mode: 0 BOOST | 1 OUT | 2 CUT, type: 0 SHELF | 1 BELL, gain_step: 0..15, bw_step: 0..15, freq_idx: 0..10}
  eq_in: bool, trim_idx: 0..10 (-2.5 .. +2.5 dB), lp_idx: 0 OFF | 1 52K | 2 40K | 3 27K | 4 20K | 5 15K, hp_idx: 0 OFF | 1 12 | 2 16 | 3 23 | 4 30 | 5 39
Functions: analog_H(params, f) exact analog response (direct complex evaluation of the network); zpk(params) poles and zeros from the
state-space eigenvalues (the C++ method, with numpy's eigenvalue solver); design(params, fs) the plugin's digital design, mirrored;
digital_H(sos, gain, f, fs) and digital_H_full(sos, gain, fir, f, fs) its response.
The fitted constants are read from src/dsp/FittedConstants.hpp, the same file the plugin compiles, so the two cannot drift apart."""
import os, re, cmath, math, numpy as np
from scipy import signal
HERE = os.path.dirname(os.path.abspath(__file__))
CONSTANTS_HPP = os.path.join(HERE, "..", "src", "dsp", "FittedConstants.hpp")

def _read_constants(path):
    """{name: float or [floats]} from the 'static constexpr double' lines of FittedConstants.hpp (the literals round-trip exactly)."""
    K = {}
    for name, arr, val in re.findall(r"static constexpr double (\w+)(\[\d+\])?\s*=\s*(\{[^}]*\}|[^;]+);", open(path).read()):
        K[name] = [float(x) for x in val.strip("{} ").split(",")] if arr else float(val)
    return K

K = _read_constants(CONSTANTS_HPP)
FREQS = {1: [22, 33, 47, 68, 100, 150, 220, 330, 470, 680, 1000], 2: [82, 120, 180, 270, 390, 560, 820, 1200, 1800, 2700, 3900],
         3: [220, 330, 470, 680, 1000, 1500, 2200, 3300, 4700, 6800, 10000], 4: [560, 820, 1200, 1800, 2700, 3900, 5600, 8200, 12000, 16000, 27000]}
HP_HZ = [None, 12.0, 16.0, 23.0, 30.0, 39.0]; LP_HZ = [None, 52000.0, 40000.0, 27000.0, 20000.0, 15000.0]
TRIM_DB = [-2.5 + 0.5 * i for i in range(11)]
W_REF = 2 * math.pi * 1000.0

def gain_a(step):
    th = step / 15.0; b = K["gain_taper_b"]
    return K["a_max_mastering"] * (1 - b ** -th) / (1 - b ** -1)

def taper3(anch, th):
    """log-resistance quadratic through the three anchors [CCW, 12:00, CW] at theta 0, 0.5, 1."""
    y0, y1, y2 = (math.log(v) for v in anch)
    c = 2 * (y2 - 2 * y1 + y0); b = y2 - y0 - c
    return math.exp(y0 + b * th + c * th * th)

def lin3(anch, th):
    return anch[0] + (anch[1] - anch[0]) * th / 0.5 if th <= 0.5 else anch[1] + (anch[2] - anch[1]) * (th - 0.5) / 0.5

def branches(params):
    """-> (boost_branches, cut_branches); a branch is ('bp2', G, a1, a0) Y = G s/(s^2+a1 s+a0) | ('hp1', D, alpha) Y = D s/(s+alpha) |
    ('lp1', G, beta) Y = G/(s+beta); all in rad/s."""
    Bb, Cb = [], []
    for k, bd in enumerate(params["bands"], start=1):
        if bd["mode"] == 1:
            continue
        g = bd["a"] if "a" in bd else gain_a(bd["gain_step"])          # "a", "theta": direct pot positions (validation of the regular unit)
        if g <= 0.0:
            continue
        th = bd["theta"] if "theta" in bd else bd["bw_step"] / 15.0
        fz = FREQS[k][bd["freq_idx"]]; w0 = 2 * math.pi * fz; src = g * (1 - g) * K["Rp"]
        boost = bd["mode"] == 0
        own, opp = (Bb, Cb) if boost else (Cb, Bb)
        if bd["type"] == 1:  # BELL
            if boost:
                R = taper3(K["bell_boost_R"], th) + src; m = 1.0
            else:
                R = taper3(K["bell_cut_R"], th) + src; m = K["m_cut"]
            X = K["X0"]; own.append(("bp2", m * g * w0 / X, R * w0 / X, w0 * w0))
            continue
        # SHELF
        m_own = 1.0 if boost else K["m_cut_shelf"]
        high = k >= 3
        special = (k == 1 and fz in (22, 33))
        air = (k == 4 and fz in (16000, 27000))
        if high:
            Rs, Xs = K["hs_Rs"] + src, K["hs_Xs"]
            if air:
                w50 = 2 * math.pi * K["air_lpf_hz"]; Lr = K["hs_Rs"] / w50   # series L normalised so its reactance equals Rs at 50 kHz
                own.append(("bp2", m_own * g / Lr, Rs / Lr, Xs * w0 / Lr))
            else:
                own.append(("hp1", m_own * g / Rs, Xs * w0 / Rs))
            dipR = taper3(K["hs_dip_R"], th)
        else:
            if special:
                Rs0, Xs = (K["sp22_Rs"], K["sp22_Xs"]) if fz == 22 else (K["sp33_Rs"], K["sp33_Xs"])
                Kc = lin3(K["sp_K"], th); Rs = Rs0 + lin3(K["sp_Rx"], th) + src
                if Kc > 1e-9:
                    own.append(("bp2", m_own * g * w0 / Xs, Rs * w0 / Xs, Kc * w0 * w0 / Xs))
                else:
                    own.append(("lp1", m_own * g * w0 / Xs, Rs * w0 / Xs))
                Gh = lin3(K["sp_Gh"], th)
                if Gh > 1e-9:   # the bandwidth low cut: series R-L in the opposite section
                    Rh = 1.0 / Gh + src; mh = K["m_cut"] if boost else 1.0
                    opp.append(("lp1", mh * g * w0 / K["sp_Xh"], Rh * w0 / K["sp_Xh"]))
                continue
            Rs, Xs = K["ls_Rs"] + src, K["ls_Xs"]
            own.append(("lp1", m_own * g * w0 / Xs, Rs * w0 / Xs))
            dipR = taper3(K["ls_dip_R"], th)
        # the band's own bell in the opposite section (dip while boosting, bump while cutting)
        wd = 2 * math.pi * K["air_dip_hz"] if air else w0; X = K["X0"]
        if boost:
            R = dipR + src; m = K["m_cut"]
        else:
            R = dipR + K["bump_dR"] + src; m = 1.0
        opp.append(("bp2", m * g * wd / X, R * wd / X, wd * wd))
    return Bb, Cb

def Y_of(brs, s):
    Y = 0j * s
    for b in brs:
        if b[0] == "bp2": Y = Y + b[1] * s / (s * s + b[2] * s + b[3])
        elif b[0] == "hp1": Y = Y + b[1] * s / (s + b[2])
        else: Y = Y + b[1] / (s + b[2])
    return Y

def cheb(order, fc, btype):
    z, p, k = signal.cheby1(order, K["filter_ripple_db"], 1.0, btype=btype, analog=True, output="zpk")
    eps = math.sqrt(10 ** (K["filter_ripple_db"] / 10) - 1)
    w3 = math.cosh(math.acosh(1 / eps) / order)          # the -3 dB point of the ripple-normalised prototype, |T_n(w3)| = 1/eps
    x3 = w3 if btype == "lowpass" else 1.0 / w3
    sc = 2 * math.pi * fc / x3
    return z * sc, p * sc, k * (sc ** (len(p) - len(z)))

def filters(params):
    out = []
    if params["hp_idx"]: out.append(cheb(3, HP_HZ[params["hp_idx"]], "highpass"))
    if params["lp_idx"]: out.append(cheb(5 if params["lp_idx"] == 1 else 3, LP_HZ[params["lp_idx"]], "lowpass"))
    return out

def analog_H(params, f):
    s = 2j * np.pi * np.asarray(f, float)
    if not params["eq_in"]: return np.ones_like(s)
    Bb, Cb = branches(params); p = K["p"]
    YB = Y_of(Bb, s); YC = Y_of(Cb, s)
    H = (1 + p) * (1 + YB) / (1 + p + YB) / (1 + YC)
    for z, pp, k in filters(params):
        H = H * k * np.prod([s - q for q in z], axis=0) / np.prod([s - q for q in pp], axis=0)
    return H * 10 ** (TRIM_DB[params["trim_idx"]] / 20)

def ss(brs):
    n = sum(2 if b[0] == "bp2" else 1 for b in brs); A = np.zeros((n, n)); bb = np.zeros(n); c = np.zeros(n); d = 0.0; i = 0
    for b in brs:
        if b[0] == "bp2":
            w = math.sqrt(b[3]) / W_REF; A[i, i + 1] = w; A[i + 1, i] = -w; A[i + 1, i + 1] = -b[2] / W_REF; bb[i + 1] = 1.0; c[i + 1] = b[1] / W_REF; i += 2
        elif b[0] == "hp1":
            A[i, i] = -b[2] / W_REF; bb[i] = 1.0; c[i] = -b[1] * b[2] / W_REF; d += b[1]; i += 1
        else:
            A[i, i] = -b[2] / W_REF; bb[i] = 1.0; c[i] = b[1] / W_REF; i += 1
    return A, bb, c, d

def zpk(params):
    Z, P, G = [], [], 1.0
    if not params["eq_in"]: return np.array([]), np.array([]), 1.0
    p = K["p"]; Bb, Cb = branches(params)
    if Bb:
        A, b, c, d = ss(Bb)
        Z += list(np.linalg.eigvals(A - np.outer(b, c) / (1 + d)) * W_REF); P += list(np.linalg.eigvals(A - np.outer(b, c) / (1 + p + d)) * W_REF)
        G *= (1 + p) * (1 + d) / (1 + p + d)
    if Cb:
        A, b, c, d = ss(Cb)
        Z += list(np.linalg.eigvals(A) * W_REF); P += list(np.linalg.eigvals(A - np.outer(b, c) / (1 + d)) * W_REF); G *= 1 / (1 + d)
    for z, pp, k in filters(params):
        Z += list(z); P += list(pp); G *= k
    return np.array(Z, complex), np.array(P, complex), G * 10 ** (TRIM_DB[params["trim_idx"]] / 20)

def digital_H(sos, gain, f, fs):
    z1 = np.exp(-2j * np.pi * np.asarray(f, float) / fs); H = gain * np.ones_like(z1)
    for b0, b1, b2, a1, a2 in sos:
        H = H * (b0 + b1 * z1 + b2 * z1 * z1) / (1 + a1 * z1 + a2 * z1 * z1)
    return H

def sections(Z, P, fs):
    """one pass over every pole, the one nearest the unit circle first (complex pairs and real poles together); each takes the zeros
    nearest to it; zeros left over become FIR sections. -> list of (analog zeros, analog poles)."""
    T = 1.0 / fs; zm = lambda q: cmath.exp(q * T)
    def split(roots):
        cx, re = [], []
        for r in roots:
            r = complex(r)
            if abs(r.imag) <= 1e-9 * max(1.0, abs(r)): re.append(r.real)
            elif r.imag > 0: cx.append(r)
        return cx, re
    pc, pr = split(P); zc, zr = split(Z); usedC = [False] * len(pc); usedR = [False] * len(pr); secs = []
    while not (all(usedC) and all(usedR)):
        cands = [(abs(zm(p)), "c", i) for i, p in enumerate(pc) if not usedC[i]] + [(math.exp(p * T), "r", i) for i, p in enumerate(pr) if not usedR[i]]
        _, kind, i = max(cands, key=lambda t: t[0])
        if kind == "c":
            usedC[i] = True; ps = [pc[i], pc[i].conjugate()]
        else:
            usedR[i] = True; ps = [complex(pr[i])]
            rest = [(abs(math.exp(pr[j] * T) - math.exp(pr[i] * T)), j) for j in range(len(pr)) if not usedR[j]]
            if rest:
                j = min(rest)[1]; usedR[j] = True; ps.append(complex(pr[j]))
        zp = zm(ps[0]); zs = []
        ci = min(range(len(zc)), key=lambda k: abs(zm(zc[k]) - zp)) if (len(ps) == 2 and zc) else None
        order = sorted(range(len(zr)), key=lambda k: abs(zm(complex(zr[k])) - zp))
        have = min(len(zr), len(ps))
        if ci is not None and (have == 0 or abs(zm(zc[ci]) - zp) <= abs(zm(complex(zr[order[0]])) - zp)):
            q = zc.pop(ci); zs = [q, q.conjugate()]
        elif have >= 1:
            pick = order[:have]; zs = [complex(zr[k]) for k in pick]
            for k in sorted(pick, reverse=True): zr.pop(k)
        secs.append((zs, ps))
    while zc:
        q = zc.pop(); secs.append(([q, q.conjugate()], []))
    while zr:
        a = zr.pop(); zs = [complex(a)]
        if zr: zs.append(complex(zr.pop()))
        secs.append((zs, []))
    return secs

def design(params, fs, fft_size=1024, taps=64):
    """the plugin's design: matched-z sections (every pole and zero mapped by z = exp(sT)) and a minimum-phase FIR correction from the
    cepstrum of log(|H_analog| / |H_matched|). -> (sos list, gain, fir taps)"""
    Z, P, G = zpk(params)
    fir = np.zeros(taps); fir[0] = 1.0
    if len(P) == 0 and len(Z) == 0: return [], G, fir
    T = 1.0 / fs; wr = 2 * math.pi * min(1000.0, 0.05 * fs); g = G; sos = []
    for zs, ps in sections(Z, P, fs):
        zd = [cmath.exp(q * T) for q in zs]; pd = [cmath.exp(q * T) for q in ps]
        b = [1.0, 0.0, 0.0]; a = [0.0, 0.0]
        if len(zd) == 2: b = [1.0, -(zd[0] + zd[1]).real, (zd[0] * zd[1]).real]
        elif len(zd) == 1: b = [1.0, -zd[0].real, 0.0]
        if len(pd) == 2: a = [-(pd[0] + pd[1]).real, (pd[0] * pd[1]).real]
        elif len(pd) == 1: a = [-pd[0].real, 0.0]
        e1 = cmath.exp(-1j * wr * T); hd = (b[0] + b[1] * e1 + b[2] * e1 * e1) / (1 + a[0] * e1 + a[1] * e1 * e1)
        ha = 1.0 + 0j
        for q in zs: ha *= (1j * wr - q)
        for q in ps: ha /= (1j * wr - q)
        g *= abs(ha) / abs(hd); sos.append((b[0], b[1], b[2], a[0], a[1]))
    half = fft_size // 2; dw = 2 * math.pi * fs / fft_size
    w = np.arange(half + 1) * dw; w[0] = 1e-3 * dw
    L = np.full(half + 1, math.log(abs(G)) - math.log(abs(g)))
    for q in Z: L += np.log(np.abs(1j * w - q)) - np.log(np.abs(np.expm1((q - 1j * w) * T)))
    for q in P: L -= np.log(np.abs(1j * w - q)) - np.log(np.abs(np.expm1((q - 1j * w) * T)))
    full = np.concatenate([L, L[-2:0:-1]]); c = np.real(np.fft.ifft(full))
    cm = np.zeros(fft_size); cm[0] = c[0]; cm[1:half] = 2 * c[1:half]; cm[half] = c[half]
    fir = np.real(np.fft.ifft(np.exp(np.fft.fft(cm))))[:taps]
    return sos, g, fir

def digital_H_full(sos, gain, fir, f, fs):
    return digital_H(sos, gain, f, fs) * np.polyval(np.asarray(fir)[::-1], np.exp(-2j * np.pi * np.asarray(f, float) / fs))
