#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
"""Pedalboard test of the built VST3: impulse responses at a grid of settings -> magnitude and phase.
 * magnitude against the analog network (tests/ref_model.py analog_H, the same equations as src/dsp/Network.hpp),
 * phase against the minimum-phase response of that magnitude on the digital band (cepstral construction, 2^16 points): the plugin is
   meant to BE that minimum-phase filter; the difference from the analog network's own phase is reported too (it comes only from the
   network's response above Nyquist),
 * latency (the plugin reports 0; the impulse response starts at sample 0), LINK, POWER, IN, stereo independence, all five filters.
usage: python3 tests/pb_ir_test.py <bundle> [out json]"""
import sys, os, json, itertools, random, numpy as np, pedalboard
bundle = sys.argv[1]; outjson = sys.argv[2] if len(sys.argv) > 2 else None
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_model as M
N = 1 << 16
P = pedalboard.load_plugin(bundle)
names = list(P.parameters.keys())
MODE = ["BOOST", "OUT", "CUT"]; TYPE = ["SHELF", "BELL"]; LP = ["OFF", "52K", "40K", "27K", "20K", "15K"]; HP = ["OFF", "12", "16", "23", "30", "39"]

def apply(ch, s):
    for b, bd in enumerate(s["bands"], start=1):
        setattr(P, f"{ch}_band{b}_mode", MODE[bd["mode"]]); setattr(P, f"{ch}_band{b}_type", TYPE[bd["type"]])
        setattr(P, f"{ch}_band{b}_gain_step", float(bd["gain_step"])); setattr(P, f"{ch}_band{b}_bandwidth", float(bd["bw_step"]))
        setattr(P, f"{ch}_band{b}_freq_hz", float(M.FREQS[b][bd["freq_idx"]]))
    setattr(P, f"{ch}_in", bool(s["eq_in"])); setattr(P, f"{ch}_gain_db", float(M.TRIM_DB[s["trim_idx"]]))
    setattr(P, f"{ch}_lowpass", LP[s["lp_idx"]]); setattr(P, f"{ch}_highpass", HP[s["hp_idx"]])

def flat():
    return {"bands": [{"mode": 1, "type": 1, "gain_step": 0, "bw_step": 7, "freq_idx": 5} for _ in range(4)], "eq_in": True, "trim_idx": 5, "lp_idx": 0, "hp_idx": 0}

def ir(sL, sR, fs, link=False, power=True):
    apply("l", sL); apply("r", sR); P.link = link; P.power = power
    x = np.zeros((2, N), np.float32); x[:, 0] = 1.0
    y = P.process(x, fs, reset=True)
    return y

def minphase_of(mag, n_dc_zeros, fs):
    """minimum-phase spectrum with magnitude mag on the N/2+1 bins 0..Nyquist. Zeros at DC (the high pass) sit on the unit circle,
    where the cepstrum is singular: they are divided out as the exact digital factor (1 - z^-1)^n, the remainder is folded, and the
    factor is put back."""
    w = np.arange(N // 2 + 1) * 2 * np.pi / N; w[0] = 1e-3 * 2 * np.pi / N
    fac = (1 - np.exp(-1j * w)) ** n_dc_zeros
    L = np.log(np.maximum(mag / np.abs(fac), 1e-300)); full = np.concatenate([L, L[-2:0:-1]])
    c = np.real(np.fft.ifft(full)); c[1:N // 2] *= 2; c[N // 2 + 1:] = 0
    return np.exp(np.fft.fft(c))[: N // 2 + 1] * fac

def check(s, fs, y):
    Y = np.fft.rfft(y.astype(np.float64)); f = np.arange(N // 2 + 1) * fs / N; f[0] = 1e-3 * fs / N
    Ha = M.analog_H(s, f); top = min(20000.0, 0.45 * fs); sel = (f >= 10) & (f <= top) & (np.abs(Ha) > 1e-3)
    mag_err = np.abs(20 * np.log10(np.abs(Y[sel])) - 20 * np.log10(np.abs(Ha[sel])))
    Hm = minphase_of(np.abs(Ha), 3 if s["hp_idx"] else 0, fs); ph_mp = np.abs(np.angle(Y[sel] / Hm[sel]))
    ph_an = np.abs(np.angle(Y[sel] / Ha[sel]))
    lo = sel & (f <= 5000)
    return {"mag_err_max_db": float(mag_err.max()), "phase_vs_minphase_max_deg": float(np.degrees(ph_mp.max())),
            "phase_vs_analog_max_deg_to_5k": float(np.degrees(np.abs(np.angle(Y[lo] / Ha[lo])).max())) if lo.any() else 0.0,
            "phase_vs_analog_max_deg_to_top": float(np.degrees(ph_an.max()))}

res = {"parameters": names, "cases": []}
rng = random.Random(20260926)
def rnd(filters=True):
    return {"bands": [{"mode": rng.choice([0, 1, 2]), "type": rng.choice([0, 1]), "gain_step": rng.randint(0, 15), "bw_step": rng.randint(0, 15), "freq_idx": rng.randint(0, 10)} for _ in range(4)],
            "eq_in": True, "trim_idx": rng.randint(0, 10), "lp_idx": rng.randint(0, 5) if filters else 0, "hp_idx": rng.randint(0, 5) if filters else 0}
# a grid: each band alone, bell and shelf, boost and cut, three bandwidths, three gains, three frequencies
grid = []
for b, typ, mode, bw, g, fi in itertools.product(range(4), (0, 1), (0, 2), (0, 7, 15), (4, 10, 15), (0, 5, 10)):
    s = flat(); s["bands"][b].update(mode=mode, type=typ, gain_step=g, bw_step=bw, freq_idx=fi); grid.append(s)
for lp in range(1, 6):
    s = flat(); s["lp_idx"] = lp; grid.append(s)
for hp in range(1, 6):
    s = flat(); s["hp_idx"] = hp; grid.append(s)
for i in range(60): grid.append(rnd())
stats = {}
for fs in (44100.0, 48000.0, 96000.0):
    worst = {"mag_err_max_db": 0, "phase_vs_minphase_max_deg": 0, "phase_vs_analog_max_deg_to_5k": 0, "phase_vs_analog_max_deg_to_top": 0}
    for s in grid:
        y = ir(s, flat(), fs)
        r = check(s, fs, y[0])
        for k in worst: worst[k] = max(worst[k], r[k])
        res["cases"].append({"fs": fs, "setting": s, **r})
    stats[fs] = worst
    print(f"fs {fs:.0f}: {len(grid)} settings on L: max |dB| vs analog {worst['mag_err_max_db']:.4f}; max phase vs min-phase of the analog magnitude "
          f"{worst['phase_vs_minphase_max_deg']:.3f} deg; phase vs the analog network itself: to 5 kHz {worst['phase_vs_analog_max_deg_to_5k']:.2f} deg, to 20 kHz {worst['phase_vs_analog_max_deg_to_top']:.2f} deg")
res["stats"] = {str(k): v for k, v in stats.items()}
# channel independence, LINK, POWER, IN, latency
fs = 48000.0; a = rnd(); b = rnd()
y = ir(a, b, fs); ra, rb = check(a, fs, y[0]), check(b, fs, y[1])
yl = ir(a, b, fs, link=True); rl = check(a, fs, yl[1])
yp = ir(a, b, fs, power=False); bypass_power = bool(np.allclose(yp[:, 0], 1.0) and np.allclose(yp[:, 1:], 0.0))
ain = dict(a); ain["eq_in"] = False; yi = ir(ain, b, fs); in_off_left = bool(np.allclose(yi[0, 0], 1.0) and np.allclose(yi[0, 1:], 0.0)); rbi = check(b, fs, yi[1])
lat = getattr(P, "reported_latency_samples", None)
peak = int(np.argmax(np.abs(y[0]))); first = int(np.argmax(np.abs(y[0]) > 1e-6))
extra = {"stereo_L_err_db": ra["mag_err_max_db"], "stereo_R_err_db": rb["mag_err_max_db"], "link_R_follows_L_err_db": rl["mag_err_max_db"],
         "power_off_is_bypass": bypass_power, "L_in_off_is_bypass": in_off_left, "R_unaffected_by_L_in_err_db": rbi["mag_err_max_db"],
         "reported_latency_samples": lat, "ir_first_nonzero_sample": first, "ir_peak_sample": peak}
res["extra"] = extra
print("stereo: L err %.4f dB, R err %.4f dB (different settings); LINK: R = L's setting, err %.4f dB; POWER off bypass: %s; L IN off bypass: %s (R still EQ'd, err %.4f dB)"
      % (ra["mag_err_max_db"], rb["mag_err_max_db"], rl["mag_err_max_db"], bypass_power, in_off_left, rbi["mag_err_max_db"]))
print(f"latency: reported {lat}; impulse response first nonzero sample {first}, peak at sample {peak}")
if outjson: json.dump(res, open(outjson, "w"))
ok = all(v["mag_err_max_db"] < 0.02 and v["phase_vs_minphase_max_deg"] < 0.5 for v in stats.values()) and ra["mag_err_max_db"] < 0.02 and rb["mag_err_max_db"] < 0.02 \
     and rl["mag_err_max_db"] < 0.02 and bypass_power and in_off_left and rbi["mag_err_max_db"] < 0.02 and first == 0 and (lat in (None, 0))
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
