#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-language check: the C++ network (src/dsp/Network.hpp) against the Python reference model (tests/ref_model.py), on the 300
settings that tests/test_dsp.cpp writes to build/tests/cpp_reference.json. Prints the largest analog difference (round-off), the C++
digital design's error against the Python analog network, and the Python mirror of the design (numpy's eigenvalue solver) against the
C++ design. usage: python3 tests/crosscheck.py [path to cpp_reference.json]"""
import json, sys, os, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ref_model as M
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "tests", "cpp_reference.json")
J = json.load(open(path))
f = np.array(J["freqs"]); worst_a = 0.0; worst_ph = 0.0; worst_d = 0.0
for c in J["cases"]:
    P = c["setting"]; fs = c["fs"]
    Ha_py = M.analog_H(P, f); Ha_cpp = np.array([complex(*v) for v in c["analog"]]); Hd = np.array([complex(*v) for v in c["digital"]])
    worst_a = max(worst_a, np.max(np.abs(20 * np.log10(np.abs(Ha_cpp)) - 20 * np.log10(np.abs(Ha_py)))))
    worst_ph = max(worst_ph, np.max(np.abs(np.angle(Ha_cpp / Ha_py))))
    sel = f < min(20000.0, 0.45 * fs)
    worst_d = max(worst_d, np.max(np.abs(20 * np.log10(np.abs(Hd[sel])) - 20 * np.log10(np.abs(Ha_py[sel])))))
print(f"{len(J['cases'])} settings: C++ analog vs Python analog: max |dB| {worst_a:.3e}, max phase {worst_ph:.3e} rad")
print(f"C++ digital design vs Python analog network (10 Hz..min(20 kHz, 0.45 fs)): max |dB| {worst_d:.4f}")
ok = worst_a < 1e-6 and worst_ph < 1e-6 and worst_d < 0.01
# the Python mirror of the plugin's design (ref_model.design) against the C++ design, same settings and rates
worst_pd = 0.0
for c in J["cases"]:
    P = c["setting"]; fs = c["fs"]; sel = f < 0.5 * fs
    sos, g, fir = M.design(P, fs); Hp = M.digital_H_full(sos, g, fir, f[sel], fs); Hd = np.array([complex(*v) for v in c["digital"]])[sel]
    worst_pd = max(worst_pd, np.max(np.abs(20 * np.log10(np.abs(Hp)) - 20 * np.log10(np.abs(Hd)))))
print(f"Python mirror of the design vs C++ design: max |dB| {worst_pd:.3e}")
ok = ok and worst_pd < 1e-6
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
