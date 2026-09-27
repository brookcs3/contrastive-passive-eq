#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Real-time cost through Pedalboard: 60 s of stereo noise at 48 kHz with every band engaged on both channels plus both filters, in one
call and in 480 calls of 100 ms with a control changed before each call (a design, a warm-up and a crossfade every 100 ms).
usage: python3 tests/pb_bench.py <bundle>"""
import sys, time, numpy as np, pedalboard
P = pedalboard.load_plugin(sys.argv[1]); fs = 48000.0
rng = np.random.default_rng(1); x = (0.1 * rng.standard_normal((2, int(60 * fs)))).astype(np.float32)
for ch in ("l", "r"):
    for b, (typ, fq) in enumerate((("SHELF", 100.0), ("BELL", 390.0), ("BELL", 3300.0), ("SHELF", 16000.0)), start=1):
        setattr(P, f"{ch}_band{b}_mode", "BOOST" if b != 2 else "CUT"); setattr(P, f"{ch}_band{b}_type", typ)
        setattr(P, f"{ch}_band{b}_gain_step", 6.0); setattr(P, f"{ch}_band{b}_bandwidth", 7.0); setattr(P, f"{ch}_band{b}_freq_hz", fq)
    setattr(P, f"{ch}_highpass", "16"); setattr(P, f"{ch}_lowpass", "27K")
t = time.perf_counter(); y = P.process(x, fs); dt = time.perf_counter() - t
print(f"steady: 60 s of stereo audio in {dt:.3f} s -> {60 / dt:.0f}x real time; output finite: {bool(np.isfinite(y).all())}")
blk = int(0.1 * fs); t = time.perf_counter(); outs = []
for i in range(0, x.shape[1], blk):
    P.l_band3_gain_step = float(4 + (i // blk) % 6)
    outs.append(P.process(x[:, i:i + blk], fs, reset=False))
dt2 = time.perf_counter() - t; y2 = np.concatenate(outs, axis=1)
print(f"a control change every 100 ms: 60 s in {dt2:.3f} s -> {60 / dt2:.0f}x real time; output finite: {bool(np.isfinite(y2).all())}, peak {np.abs(y2).max():.3f}")
# denormal check: a burst then 59 s of digital silence must cost no more than noise does (flush-to-zero is on in run())
z = np.zeros_like(x); z[:, : int(fs)] = x[:, : int(fs)]
t = time.perf_counter(); yz = P.process(z, fs); dt3 = time.perf_counter() - t
print(f"1 s burst then 59 s of silence: {dt3:.3f} s ({dt3 / dt:.2f} x the noise run); tail peak {np.abs(yz[:, -int(fs):]).max():.3g}")
